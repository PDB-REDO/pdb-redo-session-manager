/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 NKI/AVL, Netherlands Cancer Institute
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "data-service.hpp"
#include "prsm-db-connection.hpp"
#include "run-service.hpp"
#include "user-service.hpp"

#include "revision.hpp"
#include "mrsrc.hpp"

#include <condition_variable>
#include <charconv>
#include <functional>
#include <iostream>
#include <thread>
#include <tuple>

#include <zeep/crypto.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/login-controller.hpp>
#include <zeep/http/rest-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/http/uri.hpp>

#include <pqxx/pqxx>

#include <mcfp.hpp>

namespace zh = zeep::http;
namespace fs = std::filesystem;

using json = zeep::json::element;

// --------------------------------------------------------------------

class entry_class_expression_object : public zh::expression_utility_object<entry_class_expression_object>
{
  public:
	static constexpr const char *name() { return "entry"; }

	virtual zh::object evaluate(const zh::scope &scope, const std::string &methodName,
		const std::vector<zh::object> &parameters) const
	{
		zh::object result;

		try
		{
			if (methodName == "class" and parameters.size() == 2)
			{
				result = "";

				if (parameters.front().is_number() and parameters.back().is_number())
				{
					double o = parameters.front().as<double>();
					double f = parameters.back().as<double>();

					if (o > 1.0)
					{
						if (f < o)
							result = "better";
						else if (f > o)
							result = "worse";
					}
					else if (f > 1.0)
						result = "worse";
				}
			}
			else if (methodName == "percClass" and parameters.size() == 2)
			{
				result = "";

				if (parameters.front().is_number() and parameters.back().is_number())
				{
					double o = parameters.front().as<double>();
					double f = parameters.back().as<double>();

					if (f == 100 || f > o)
						result = "better";
					else if (f < o)
						result = "worse";
				}
			}
			else if (methodName == "rffinClass" and parameters.size() == 1)
			{
				result = "";

				auto &data = parameters.front();

				if (data["RFFIN"].is_number() and data["SIGRFCAL"].is_number())
				{
					auto rffin = data["RFFIN"].as<double>();
					auto sigrfcal = data["SIGRFCAL"].as<double>();

					if (data["RFREE"].is_null() or data["ZCALERR"] == true or data["TSTCNT"] != data["NTSTCNT"])
					{
						if (data["RFCALUNB"].is_number())
						{
							auto rfcalunb = data["RFCALUNB"].as<double>();

							if (rffin < (rfcalunb - 2.6 * sigrfcal))
								result = "better";
							else if (rffin > (rfcalunb + 2.6 * sigrfcal))
								result = "worse";
						}
					}
					else if (data["RFCAL"].is_number())
					{
						double rfcal = data["RFCAL"].as<double>();

						if (rffin < (rfcal - 2.6 * sigrfcal))
							result = "better";
						else if (rffin > (rfcal + 2.6 * sigrfcal))
							result = "worse";
					}
				}
			}
		}
		catch (const std::exception &ex)
		{
			std::cerr << ex.what() << std::endl;
		}

		return result;
	}

} s_entry_class_expression_object;

// --------------------------------------------------------------------

struct Session
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string token;
	std::chrono::time_point<std::chrono::system_clock> created;
	std::chrono::time_point<std::chrono::system_clock> expires;

	Session &operator=(const pqxx::row &row)
	{
		id = row.at("id").as<unsigned long>();
		name = row.at("name").as<std::string>();
		user = row.at("user").as<std::string>();
		token = row.at("token").as<std::string>();
		created = zeep::value_serializer<std::chrono::time_point<std::chrono::system_clock>>::from_string(row.at("created").as<std::string>());
		expires = zeep::value_serializer<std::chrono::time_point<std::chrono::system_clock>>::from_string(row.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const { return id != 0; }

	bool expired() const { return std::chrono::system_clock::now() > expires; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar &zeep::make_nvp("id", id) & zeep::make_nvp("name", name) & zeep::make_nvp("user", user) & zeep::make_nvp("token", token) & zeep::make_nvp("created", created) & zeep::make_nvp("expires", expires);
	}
};

struct CreateSessionResult
{
	unsigned long id = 0;
	std::string name;
	std::string token;
	std::chrono::time_point<std::chrono::system_clock> expires;

	CreateSessionResult(const Session &session)
		: id(session.id)
		, name(session.name)
		, token(session.token)
		, expires(session.expires)
	{
	}

	CreateSessionResult(const pqxx::row &row)
		: id(row.at("id").as<unsigned long>())
		, name(row.at("name").as<std::string>())
		, token(row.at("token").as<std::string>())
		, expires(zeep::value_serializer<std::chrono::time_point<std::chrono::system_clock>>::from_string(row.at("expires").as<std::string>()))
	{
	}

	CreateSessionResult &operator=(const Session &session)
	{
		id = session.id;
		name = session.name;
		token = session.token;
		expires = session.expires;

		return *this;
	}

	CreateSessionResult &operator=(const pqxx::row &row)
	{
		id = row.at("id").as<unsigned long>();
		name = row.at("name").as<std::string>();
		token = row.at("token").as<std::string>();
		expires = zeep::value_serializer<std::chrono::time_point<std::chrono::system_clock>>::from_string(row.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const { return id != 0; }

	bool expired() const { return std::chrono::system_clock::now() > expires; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar &zeep::make_nvp("id", id) & zeep::make_nvp("name", name) & zeep::make_nvp("token", token) & zeep::make_nvp("expires", expires);
	}
};

// --------------------------------------------------------------------

class SessionStore
{
  public:
	static void init()
	{
		sInstance = new SessionStore();
	}

	static SessionStore &instance()
	{
		return *sInstance;
	}

	void stop()
	{
		delete sInstance;
		sInstance = nullptr;
	}

	Session create(const std::string &name, const std::string &user);

	Session get_by_id(unsigned long id);
	Session get_by_token(const std::string &token);
	void delete_by_id(unsigned long id);

	std::vector<Session> get_all_sessions();

  private:
	SessionStore();
	~SessionStore();

	SessionStore(const SessionStore &) = delete;
	SessionStore &operator=(const SessionStore &) = delete;

	void run_clean_thread();

	bool m_done = false;

	std::condition_variable m_cv;
	std::mutex m_cv_m;
	std::thread m_clean;

	static SessionStore *sInstance;
};

SessionStore *SessionStore::sInstance = nullptr;

SessionStore::SessionStore()
	: m_clean(std::bind(&SessionStore::run_clean_thread, this))
{
}

SessionStore::~SessionStore()
{
	{
		std::lock_guard<std::mutex> lock(m_cv_m);
		m_done = true;
		m_cv.notify_one();
	}

	m_clean.join();
}

void SessionStore::run_clean_thread()
{
	while (not m_done)
	{
		std::unique_lock<std::mutex> lock(m_cv_m);
		if (m_cv.wait_for(lock, std::chrono::seconds(60)) == std::cv_status::timeout)
		{
			try
			{
				pqxx::transaction tx(prsm_db_connection::instance());
				auto r = tx.exec0(R"(DELETE FROM session WHERE CURRENT_TIMESTAMP > expires)");
				tx.commit();
			}
			catch (const std::exception &ex)
			{
				std::cerr << ex.what() << std::endl;
			}
		}
	}
}

Session SessionStore::create(const std::string &name, const std::string &user)
{
	User u = UserService::instance().get_user(user);

	pqxx::transaction tx(prsm_db_connection::instance());

	std::string token = zeep::encode_base64url(zeep::random_hash());

	auto r = tx.exec1(
		R"(INSERT INTO session (user_id, name, token)
		   VALUES ()" +
		std::to_string(u.id) + ", " + tx.quote(name) + ", " + tx.quote(token) + R"()
		   RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	unsigned long tokenid = r[0].as<unsigned long>();
	std::string created = r[1].as<std::string>();

	tx.commit();

	return {
		tokenid,
		name,
		user,
		token,
		zeep::value_serializer<std::chrono::time_point<std::chrono::system_clock>>::from_string(created)
	};
}

Session SessionStore::get_by_id(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec(
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN public.user b ON a.user_id = b.id
		   WHERE a.id = )" +
		std::to_string(id));

	Session result = {};
	if (r.size() == 1)
		result = r.front();

	tx.commit();

	return result;
}

Session SessionStore::get_by_token(const std::string &token)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec(
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN public.user b ON a.user_id = b.id
		   WHERE a.token = )" +
		tx.quote(token));

	Session result = {};
	if (r.size() == 1)
		result = r.front();

	tx.commit();

	return result;
}

void SessionStore::delete_by_id(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec0(R"(DELETE FROM session WHERE id = )" + std::to_string(id));
	tx.commit();
}

std::vector<Session> SessionStore::get_all_sessions()
{
	std::vector<Session> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(
		R"(SELECT a.id AS id,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires,
				  a.name AS name,
				  b.name AS user,
				  a.token AS token
			 FROM session a, public.user b
			WHERE a.user_id = b.id
			ORDER BY a.created ASC)");

	for (auto row : rows)
	{
		Session session;
		session = row;
		result.push_back(std::move(session));
	}

	return result;
}

// --------------------------------------------------------------------

class SessionRESTController : public zh::rest_controller
{
  public:
	SessionRESTController()
		: zh::rest_controller("api")
	{
		// create a new session, user should provide username, password and session name
		map_post_request("session", &SessionRESTController::post_session, "user", "password", "name");
	}

	// CRUD routines
	CreateSessionResult post_session(std::string user, std::string password, std::string name)
	{
		User u = UserService::instance().get_user(user);
		std::string pw = u.password;

		PasswordEncoder pwenc;

		if (not pwenc.matches(password, u.password))
			throw std::runtime_error("Invalid username/password");

		std::string token = zeep::encode_base64url(zeep::random_hash());
		unsigned long userid = u.id;

		pqxx::transaction tx(prsm_db_connection::instance());
		auto t = tx.exec1(
			R"(INSERT INTO session (user_id, name, token)
			   VALUES ()" +
			std::to_string(userid) + ", " + tx.quote(name) + ", " + tx.quote(token) + R"()
			RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
					  trim(both '"' from to_json(expires)::text) AS expires)");

		tx.commit();

		return t;
	}
};

class APIRESTController : public zh::rest_controller
{
  public:
	APIRESTController()
		: zh::rest_controller("api")
	{
		// get session info
		map_get_request("session/{id}", &APIRESTController::get_session, "id");

		// delete a session
		map_delete_request("session/{id}", &APIRESTController::delete_session, "id");

		// return a list of runs
		map_get_request("session/{id}/run", &APIRESTController::get_all_runs, "id");

		// Submit a run (job)
		map_post_request("session/{id}/run", &APIRESTController::create_job, "id",
			"mtz-file", "pdb-file", "restraints-file", "sequence-file", "parameters");

		// return info for a run
		map_get_request("session/{id}/run/{run}", &APIRESTController::get_run, "id", "run");

		// get a list of the files in output
		map_get_request("session/{id}/run/{run}/output", &APIRESTController::get_result_file_list, "id", "run");

		// get all results file zipped into an archive
		map_get_request("session/{id}/run/{run}/output/zipped", &APIRESTController::get_zipped_result_file, "id", "run");

		// get a result file
		map_get_request("session/{id}/run/{run}/output/{file}", &APIRESTController::get_result_file, "id", "run", "file");

		// delete a run
		map_delete_request("session/{id}/run/{run}", &APIRESTController::delete_run, "id", "run");
	}

	virtual bool handle_request(zh::request &req, zh::reply &rep)
	{
		bool result = false;

		if (req.get_method() == "OPTIONS")
		{
			get_options(req, rep);
			result = true;
		}
		else
		{
			try
			{
				std::string authorization = req.get_header("Authorization");
				// PDB-REDO-api Credential=token-id/date/pdb-redo-apiv2,SignedHeaders=host;x-pdb-redo-content-sha256,Signature=xxxxx

				if (authorization.compare(0, 13, "PDB-REDO-api ") != 0)
					throw zh::unauthorized_exception();

				std::vector<std::string> signedHeaders;

				std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

				std::smatch m;
				if (not std::regex_search(authorization, m, re))
					throw zh::unauthorized_exception();

				std::vector<std::string> credentials;
				std::string credential = m[1].str();

				for (std::string::size_type i = 0, j = credential.find("/");;)
				{
					credentials.push_back(credential.substr(i, j - i));
					if (j == std::string::npos)
						break;
					i = j + 1;
					j = credential.find("/", i);
				}

				if (credentials.size() != 3 or credentials[2] != "pdb-redo-api")
					throw zh::unauthorized_exception();

				auto signature = zeep::decode_base64(m[3].str());

				// Validate the signature

				// canonical request

				std::vector<std::tuple<std::string, std::string>> params;
				for (auto &p : req.get_parameters())
					params.push_back(std::make_tuple(p.first, p.second));
				std::sort(params.begin(), params.end());
				std::ostringstream ps;
				auto n = params.size();
				for (auto &[name, value] : params)
				{
					ps << zeep::http::encode_url(name);
					if (not value.empty())
						ps << '=' << zeep::http::encode_url(value);
					if (n-- > 1)
						ps << '&';
				}

				auto contentHash = zeep::encode_base64(zeep::sha256(req.get_payload()));

				auto pathPart = req.get_uri();
				auto pqs = pathPart.find('?');
				if (pqs != std::string::npos)
					pathPart.erase(pqs, std::string::npos);

				// Correct for a potential context
				if (not m_server->get_context_name().empty())
				{
					zeep::http::uri uri(m_server->get_context_name());
					pathPart = fs::path("/") / uri.get_path() / fs::path(pathPart).relative_path();
				}

				std::ostringstream ss;
				ss << req.get_method() << std::endl
				   << pathPart << std::endl
				   << ps.str() << std::endl
				   << req.get_header("host") << std::endl
				   << contentHash;

				auto canonicalRequest = ss.str();
				auto canonicalRequestHash = zeep::encode_base64(zeep::sha256(canonicalRequest));

				// string to sign
				auto timestamp = req.get_header("X-PDB-REDO-Date");

				std::ostringstream ss2;
				ss2 << "PDB-REDO-api" << std::endl
					<< timestamp << std::endl
					<< credential << std::endl
					<< canonicalRequestHash;
				auto stringToSign = ss2.str();

				auto tokenid = credentials[0];
				auto date = credentials[1];

				auto secret = SessionStore::instance().get_by_id(std::stoul(tokenid)).token;
				auto keyString = "PDB-REDO" + secret;

				auto key = zeep::hmac_sha256(date, keyString);
				if (zeep::hmac_sha256(stringToSign, key) != signature)
					throw zh::unauthorized_exception();

				result = zh::rest_controller::handle_request(req, rep);
			}
			catch (const std::exception &e)
			{
				using namespace std::literals;

				rep.set_content(json({ { "error", e.what() } }));
				rep.set_status(zh::unauthorized);

				result = true;
			}
		}

		return result;
	}

	// CRUD routines

	CreateSessionResult get_session(unsigned long id)
	{
		return SessionStore::instance().get_by_id(id);
	}

	void delete_session(unsigned long id)
	{
		SessionStore::instance().delete_by_id(id);
	}

	std::vector<Run> get_all_runs(unsigned long id)
	{
		auto session = SessionStore::instance().get_by_id(id);

		return RunService::instance().get_runs_for_user(session.user);
	}

	Run create_job(unsigned long sessionID, const zh::file_param &diffractionData, const zh::file_param &coordinates,
		const zh::file_param &restraints, const zh::file_param &sequence, const json &params)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().submit(session.user, coordinates, diffractionData, restraints, sequence, params);
	}

	Run get_run(unsigned long sessionID, unsigned long runID)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().get_run(session.user, runID);
	}

	std::vector<std::string> get_result_file_list(unsigned long sessionID, unsigned long runID)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().get_result_file_list(session.user, runID);
	}

	fs::path get_result_file(unsigned long sessionID, unsigned long runID, const std::string &file)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().get_result_file(session.user, runID, file);
	}

	zh::reply get_zipped_result_file(unsigned long sessionID, unsigned long runID)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		const auto &[is, name] = RunService::instance().get_zipped_result_file(session.user, runID);

		zh::reply rep{ zh::ok };
		rep.set_content(is, "application/zip");
		rep.set_header("content-disposition", "attachement; filename = \"" + name + '"');

		return rep;
	}

	void delete_run(unsigned long sessionID, unsigned long runID)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().delete_run(session.user, runID);
	}

  private:
	fs::path m_pdb_redo_dir;
};

// --------------------------------------------------------------------

struct Stats
{
	double RFREE, RFFIN, OZRAMA, FZRAMA, OCHI12, FCHI12, URESO;

	Stats() {}
	Stats(double RFREE, double RFFIN, double OZRAMA, double FZRAMA, double OCHI12, double FCHI12, double URESO)
		: RFREE(RFREE), RFFIN(RFFIN), OZRAMA(OZRAMA), FZRAMA(FZRAMA), OCHI12(OCHI12), FCHI12(FCHI12), URESO(URESO) {}
	
	Stats(const Stats &) = default;
	Stats &operator=(const Stats &) = default;

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("RFREE", RFREE)
		   & zeep::make_nvp("RFFIN", RFFIN)
		   & zeep::make_nvp("OZRAMA", OZRAMA)
		   & zeep::make_nvp("FZRAMA", FZRAMA)
		   & zeep::make_nvp("OCHI12", OCHI12)
		   & zeep::make_nvp("FCHI12", FCHI12)
		   & zeep::make_nvp("URESO", URESO);
	}
};

class GFXRESTController : public zeep::http::rest_controller
{
  public:
	GFXRESTController()
		: zh::rest_controller("gfx")
	{
		map_get_request("statistics-for-box-plot", &GFXRESTController::get_statistics_for_box_plot, "ureso");
	}

	std::vector<Stats> get_statistics_for_box_plot(double ureso)
	{
		auto &config = mcfp::config::instance();
		fs::path toolsDir = config.get<std::string>("pdb-redo-tools-dir");
		std::ifstream f(toolsDir / "pdb_redo_stats.csv");
		if (not f.is_open())
			throw std::runtime_error("Could not open statistics file");
		
		std::string line;
		getline(f, line);	// skip first

		std::vector<Stats> stats;

		while (getline(f, line))
		{
			std::vector<std::string> fld;
			zeep::split(fld, line, ",");
			if (fld.size() != 7)
				continue;
			stats.emplace_back(stod(fld[0]), stod(fld[1]), stod(fld[2]), stod(fld[3]), stod(fld[4]), stod(fld[5]), stod(fld[6]));
		}

		sort(stats.begin(), stats.end(), [ureso](const Stats &a, const Stats &b) {
			auto ad = (a.URESO - ureso) * (a.URESO - ureso);
			auto bd = (b.URESO - ureso) * (b.URESO - ureso);
			return ad < bd;
		});

		auto mm = std::accumulate(stats.begin(), stats.begin() + 1000,
			std::tuple<double,double>{ std::numeric_limits<double>::max(), std::numeric_limits<double>::min() },
			[](std::tuple<double,double> cur, const Stats &stat)
			{
				if (std::get<0>(cur) > stat.URESO)
					std::get<0>(cur) = stat.URESO;
				if (std::get<1>(cur) < stat.URESO)
					std::get<1>(cur) = stat.URESO;
				return cur;
			});
		
		stats.erase(
			std::remove_if(stats.begin(), stats.end(),
				[mmin = std::get<0>(mm), mmax = std::get<1>(mm)](const Stats &stat) { return stat.URESO < mmin or stat.URESO > mmax; }),
			stats.end());

		return stats;
	}
};

// class db_rest_controller : public zh::rest_controller
// {
//   public:
// 	db_rest_controller()
// 		: zh::rest_controller("db")
// 	{
// 		// get a list of the files for db entry
// 		map_get_request("{id}", &db_rest_controller::get_file_list, "id");

// 		// get all files zipped into an archive
// 		map_get_request("{id}/zipped", &db_rest_controller::get_zip_file, "id");

// 		// get a result file
// 		map_get_request("{id}/{file}", &db_rest_controller::get_file, "id", "file");

// 		map_get_request("{id}/wf/pdbout.txt", &db_rest_controller::get_wf_file, "id");
// 		map_get_request("{id}/wo/pdbout.txt", &db_rest_controller::get_wo_file, "id");
// 	}

// 	std::vector<std::string> get_file_list(const std::string &pdbID)
// 	{
// 		return data_service::instance().get_file_list(pdbID);
// 	}

// 	fs::path get_file(const std::string &pdbID, const std::string &file)
// 	{
// 		return data_service::instance().get_file(pdbID, file);
// 	}

// 	fs::path get_wf_file(const std::string &pdbID)
// 	{
// 		return data_service::instance().get_file(pdbID, "wf/pdbout.txt");
// 	}

// 	fs::path get_wo_file(const std::string &pdbID)
// 	{
// 		return data_service::instance().get_file(pdbID, "wo/pdbout.txt");
// 	}

// 	zh::reply get_zip_file(const std::string &pdbID)
// 	{
// 		const auto &[is, name] = data_service::instance().get_zip_file(pdbID);

// 		zh::reply rep{ zh::ok };
// 		rep.set_content(is, "application/zip");
// 		rep.set_header("content-disposition", "attachement; filename = \"" + name + '"');

// 		return rep;
// 	}
// };

// --------------------------------------------------------------------

class JobHTMLController : public zh::html_controller
{
  public:
	JobHTMLController()
		: zh::html_controller("job")
	{
		map_get("", &JobHTMLController::get_job_listing);
		map_get("output/{job-id}/{file}", &JobHTMLController::get_output_file, "job-id", "file");
		map_get("image/{job-id}", &JobHTMLController::get_image_file, "job-id");
		map_get("result/{job-id}", &JobHTMLController::get_result, "job-id");
		map_get("entry/{job-id}", &JobHTMLController::get_entry, "job-id");
	}

	zh::reply get_job_listing(const zh::scope &scope)
	{
		auto credentials = scope.get_credentials();

		zh::scope sub(scope);

		sub.put("page", "job");

		std::error_code ec;
		json runs;

		for (auto &run : RunService::instance().get_runs_for_user(credentials["username"].as<std::string>()))
		{
			json run_j;
			to_element(run_j, run);
			runs.emplace_back(std::move(run_j));
		}
		sub.put("runs", std::move(runs));

		return get_template_processor().create_reply_from_template("job-overview", sub);
	}

	zh::reply get_output_file(const zh::scope &scope, unsigned long job_id, const std::string &file)
	{
		auto credentials = scope.get_credentials();

		auto f = RunService::instance().get_result_file(credentials["username"].as<std::string>(), job_id, file);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f), "text/plain");
		result.set_header("content-disposition", "attachement; filename = \"" + f.filename().string() + "\"");
		return result;
	}

	zh::reply get_image_file(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();

		auto f = RunService::instance().get_image_file(credentials["username"].as<std::string>(), job_id);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f, std::ios::in | std::ios::binary), "image/png");
		return result;
	}

	zh::reply get_result(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();

		auto r = RunService::instance().get_run(credentials["username"].as<std::string>(), job_id);

		zh::scope sub(scope);

		sub.put("job-id", job_id);

		return get_template_processor().create_reply_from_template("job-result", sub);
	}

	zh::reply get_entry(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();
		auto r = RunService::instance().get_run(credentials["username"].as<std::string>(), job_id);

		auto dataJsonFile = RunService::instance().get_result_file(credentials["username"].as<std::string>(), job_id, "data.json");
		std::ifstream dataJson(dataJsonFile);

		if (not dataJson.is_open())
			throw zeep::http::not_found;

		zeep::json::element data;
		zeep::json::parse_json(dataJson, data);

		auto pdbID = data["pdbid"].as<std::string>();

		zeep::json::element entry{
			{ "id", data["pdbid"] },
			{ "dbEntry", false }
		};

		entry["data"] = std::move(data["properties"]);
		entry["rama-angles"] = std::move(data["rama-angles"]);

		auto &link = entry["link"];
		fs::path dir = "/job/output/" + std::to_string(job_id);
		for (auto file : RunService::instance().get_result_file_list(credentials["username"].as<std::string>(), job_id))
		{
			if (zeep::ends_with(file, "final.pdb"))
				link["final_pdb"] = dir / file;
			else if (zeep::ends_with(file, "final.cif"))
				link["final_cif"] = dir / file;
			else if (zeep::ends_with(file, "final.mtz"))
				link["final_mtz"] = dir / file;
			else if (zeep::ends_with(file, "besttls.pdb.gz"))
				link["besttls_pdb"] = dir / file;
			else if (zeep::ends_with(file, "besttls.mtz.gz"))
				link["besttls_mtz"] = dir / file;
			else if (zeep::ends_with(file, ".refmac"))
				link["refmac_settings"] = dir / file;
			else if (zeep::ends_with(file, "homology.rest"))
				link["homology_rest"] = dir / file;
			else if (zeep::ends_with(file, "hbond.rest"))
				link["hbond_rest"] = dir / file;
			else if (zeep::ends_with(file, "metal.rest"))
				link["metal_rest"] = dir / file;
			else if (zeep::ends_with(file, "nucleic.rest"))
				link["nucleic_rest"] = dir / file;
			else if (zeep::ends_with(file, "wo/pdbout.txt"))
				link["wo"] = dir / file;
			else if (zeep::ends_with(file, "wf/pdbout.txt"))
				link["wf"] = dir / file;
			else if (file == pdbID + ".log")
				link["log"] = dir / file;
		}

		link["alldata"] = dir / "zipped";

		zh::scope sub(scope);
		sub.put("entry", entry);

		return get_template_processor().create_reply_from_template("entry::tables", sub);
	}

};

// --------------------------------------------------------------------

class RootHTMLController : public zh::html_controller
{
  public:
	RootHTMLController()
	{
		map_get("", &RootHTMLController::welcome);

		mount("admin", &RootHTMLController::admin);
		map_get("about", &RootHTMLController::about);
		map_get("privacy-policy", &RootHTMLController::gdpr);
		map_get("download", &RootHTMLController::download);

		map_delete("admin/deleteSession", &RootHTMLController::handle_delete_session, "sessionid");
		mount("{css,scripts,fonts,images}/", &RootHTMLController::handle_file);

		// map_post("job-entry", &RootHTMLController::handle_entry, "token-id", "token-secret", "job-id");
		map_post("entry", &RootHTMLController::handle_entry, "data.json", "link-url");
	}

	zh::reply welcome(const zh::scope &scope)
	{
		return get_template_processor().create_reply_from_template("index", scope);
	}

	zh::reply about(const zh::scope &scope)
	{
		return get_template_processor().create_reply_from_template("about", scope);
	}

	zh::reply gdpr(const zh::scope &scope)
	{
		return get_template_processor().create_reply_from_template("gdpr", scope);
	}

	zh::reply download(const zh::scope &scope)
	{
		return get_template_processor().create_reply_from_template("download", scope);
	}

	void admin(const zh::request &request, const zh::scope &scope, zh::reply &reply);

	zh::reply handle_delete_session(const zh::scope &scope, unsigned long sessionID);

	// zh::reply handle_entry(const zh::scope &scope, const std::string &tokenID, const std::string &tokenSecret, const std::string &jobID);
	zh::reply handle_entry(const zh::scope &scope, const zeep::json::element &data, const std::optional<std::string> &link_url);
};


void RootHTMLController::admin(const zh::request &request, const zh::scope &scope, zh::reply &reply)
{
	zh::scope sub(scope);

	sub.put("page", "admin");

	json sessions;
	auto s = SessionStore::instance().get_all_sessions();
	to_element(sessions, s);
	sub.put("sessions", sessions);

	get_template_processor().create_reply_from_template("admin.html", sub, reply);
}

zh::reply RootHTMLController::handle_delete_session(const zh::scope &scope, unsigned long sessionID)
{
	SessionStore::instance().delete_by_id(sessionID);
	return zh::reply::stock_reply(zh::ok);
}

// void RootHTMLController::handle_registration(const zh::request &request, const zh::scope &scope, zh::reply &reply)
// {
// 	get_template_processor().create_reply_from_template("register.html", scope, reply);
// }

// void RootHTMLController::handle_result(const zh::request &request, const zh::scope &scope, zh::reply &reply)
// {
// 	auto token_id = request.get_parameter("token-id");
// 	auto token_secret = request.get_parameter("token-secret");
// 	auto job_id = request.get_parameter("job-id");

// 	zh::scope sub(scope);

// 	if (not token_id.empty())
// 		sub.put("token-id", token_id);

// 	if (not token_secret.empty())
// 		sub.put("token-secret", token_secret);

// 	if (not job_id.empty())
// 		sub.put("job-id", job_id);

// 	get_template_processor().create_reply_from_template("pdb-redo-result.html", sub, reply);
// }

// zh::reply RootHTMLController::handle_entry(const zh::scope &scope, const std::string &tokenID, const std::string &tokenSecret, const std::string &jobID)
// {
// 	zeep::json::element entry;

// 	zeep::json::parse_json(request.get_parameter("data.json"), entry["data"]);

// 	zh::scope sub(scope);
// 	sub.put("entry", entry);

// 	get_template_processor().create_reply_from_template("entry::tables", sub, reply);
// }

zh::reply RootHTMLController::handle_entry(const zh::scope &scope, const zeep::json::element &data, const std::optional<std::string> &data_link)
{
	auto pdbID = data["pdbid"].as<std::string>();

	zeep::json::element entry{
		{ "id", data["pdbid"] },
		{ "dbEntry", false }
	};

	entry["data"] = std::move(data["properties"]);
	entry["rama-angles"] = std::move(data["rama-angles"]);

	if (data_link.has_value())
	{
		auto &link = entry["link"];
		zeep::http::uri db(*data_link);

		link["final_pdb"] = (db / (pdbID + "_final.pdb")).string();
		link["final_cif"] = (db / (pdbID + "_final.cif")).string();
		link["final_mtz"] = (db / (pdbID + "_final.mtz")).string();
		link["besttls_pdb"] = (db / (pdbID + "_besttls.pdb.gz")).string();
		link["besttls_mtz"] = (db / (pdbID + "_besttls.mtz.gz")).string();
		link["refmac_settings"] = (db / (pdbID + ".refmac")).string();
		link["homology_rest"] = (db / "homology.rest").string();
		link["hbond_rest"] = (db / "hbond.rest").string();
		link["metal_rest"] = (db / "metal.rest").string();
		link["nucleic_rest"] = (db / "nucleic.rest").string();
		link["wo"] = (db / "wo/pdbout.txt").string();
		link["wf"] = (db / "wf/pdbout.txt").string();

		link["alldata"] = (db / "zipped").string();
	}

	zh::scope sub(scope);
	sub.put("entry", entry);

	return get_template_processor().create_reply_from_template("entry::tables", sub);
}

// --------------------------------------------------------------------

class DBHTMLController : public zh::html_controller
{
  public:
	DBHTMLController()
		: zh::html_controller("db")
	{
		map_post("get", &DBHTMLController::handle_get, "pdb-id");

		map_get("entry", &DBHTMLController::handle_entry, "pdb-id");
		map_post("entry", &DBHTMLController::handle_entry, "pdb-id");

		map_get("{id}", &DBHTMLController::handle_show, "id");
	}

	zh::reply handle_get(const zh::scope &scope, const std::string &pdbID);
	zh::reply handle_entry(const zh::scope &scope, const std::string &pdbID);
	zh::reply handle_show(const zh::scope &scope, const std::string &pdbID);
};

zh::reply DBHTMLController::handle_get(const zh::scope &scope, const std::string &pdbID)
{
	if (pdbID.empty())
		throw std::runtime_error("Please specify a valid PDB ID");
	
	return zh::reply::redirect(pdbID);
}

zh::reply DBHTMLController::handle_show(const zh::scope &scope, const std::string &pdbID)
{
	zh::scope sub(scope);

	const auto pdbRedoVersion = data_service::instance().version();

	sub.put("pdb-id", pdbID);
	sub.put("version", pdbRedoVersion);

	auto dataJsonFile = data_service::instance().get_file(pdbID, "data.json");
	std::ifstream dataJson(dataJsonFile);

	zeep::json::element data;
	zeep::json::parse_json(dataJson, data);

	double version;
	if (mcfp::charconv<double>::from_chars(pdbRedoVersion.data(), pdbRedoVersion.data() + pdbRedoVersion.length(), version).ec != std::errc())
		version = 0;

	zeep::json::element entry{
		{ "id", pdbID },
		{ "dbEntry", true },
		{ "upToDate", data["properties"]["version"].as<double>() >= version }
	};
	sub.put("entry", std::move(entry));

	return get_template_processor().create_reply_from_template("db-entry", sub);
}

zh::reply DBHTMLController::handle_entry(const zh::scope &scope, const std::string &pdbID)
{
	auto dataJsonFile = data_service::instance().get_file(pdbID, "data.json");
	std::ifstream dataJson(dataJsonFile);

	zeep::json::element data;
	zeep::json::parse_json(dataJson, data);

	zeep::json::element entry{
		{ "id", pdbID },
		{ "dbEntry", true }
	};

	entry["data"] = std::move(data["properties"]);
	entry["rama-angles"] = std::move(data["rama-angles"]);

	auto &link = entry["link"];
	fs::path db("db/" + pdbID);
	for (auto file : data_service::instance().get_file_list(pdbID))
	{
		if (zeep::ends_with(file, "final.pdb"))
			link["final_pdb"] = db / file;
		else if (zeep::ends_with(file, "final.cif"))
			link["final_cif"] = db / file;
		else if (zeep::ends_with(file, "final.mtz"))
			link["final_mtz"] = db / file;
		else if (zeep::ends_with(file, "besttls.pdb.gz"))
			link["besttls_pdb"] = db / file;
		else if (zeep::ends_with(file, "besttls.mtz.gz"))
			link["besttls_mtz"] = db / file;
		else if (zeep::ends_with(file, ".refmac"))
			link["refmac_settings"] = db / file;
		else if (zeep::ends_with(file, "homology.rest"))
			link["homology_rest"] = db / file;
		else if (zeep::ends_with(file, "hbond.rest"))
			link["hbond_rest"] = db / file;
		else if (zeep::ends_with(file, "metal.rest"))
			link["metal_rest"] = db / file;
		else if (zeep::ends_with(file, "nucleic.rest"))
			link["nucleic_rest"] = db / file;
		else if (zeep::ends_with(file, "wo/pdbout.txt"))
			link["wo"] = db / file;
		else if (zeep::ends_with(file, "wf/pdbout.txt"))
			link["wf"] = db / file;
		else if (file == pdbID + ".log")
			link["log"] = db / file;
	}

	link["alldata"] = db / "zipped";

	zh::scope sub(scope);
	sub.put("entry", entry);

	return get_template_processor().create_reply_from_template("entry::tables", sub);
}


// --------------------------------------------------------------------

int a_main(int argc, char *const argv[])
{
	using namespace std::literals;

	int result = 0;

	auto &config = mcfp::config::instance();

	config.init(
		"usage: prsmd command [options]\n       (where command is one of 'start', 'stop', 'status' or 'reload'",
		mcfp::make_option("help,h", "Display help message"),
		mcfp::make_option("verbose,v", "Verbose output"),
		mcfp::make_option("no-daemon,F", "Do not fork into background"),
		mcfp::make_option<std::string>("config", "Specify the config file to use"),
		mcfp::make_option("version", "Print version and exit"),

		mcfp::make_option<std::string>("pdb-redo-db-dir", "Directory containing PDB-REDO databank"),
		mcfp::make_option<std::string>("pdb-redo-tools-dir", "Directory containing PDB-REDO tools (and files)"),
		mcfp::make_option<std::string>("pdb-redo-services-dir", "Directory containing PDB-REDO server data"),
		mcfp::make_option<std::string>("runs-dir", "Directory containing PDB-REDO server run directories"),
		mcfp::make_option<std::string>("ccp4-dir", "CCP4 directory, if not specified the environmental variable CCP4 will be used (and should be available)"),
		mcfp::make_option<std::string>("address", "0.0.0.0", "External address"),
		mcfp::make_option<uint16_t>("port", 10339, "Port to listen to"),
		mcfp::make_option<std::string>("context", "The outside base url for this service"),
		mcfp::make_option<std::string>("user,u", "User to run the daemon"),
		mcfp::make_option<std::string>("db-host", "Database host"),
		mcfp::make_option<std::string>("db-port", "Database port"),
		mcfp::make_option<std::string>("db-dbname", "Database name"),
		mcfp::make_option<std::string>("db-user", "Database user name"),
		mcfp::make_option<std::string>("db-password", "Database password"),
		mcfp::make_option<std::string>("admin", "Administrators, list of usernames separated by comma"),
		mcfp::make_option<std::string>("secret", "Secret value, used in signing access tokens"),

		mcfp::make_option<std::string>("smtp-user", "user name of SMTP server used for resetting password"),
		mcfp::make_option<std::string>("smtp-password", "password of SMTP server used for resetting password"),
		mcfp::make_option<std::string>("smtp-host", "host of SMTP server used for resetting password"),
		mcfp::make_option<uint16_t>("smtp-port", "port of SMTP server used for resetting password"),

		// for rama-angles
		mcfp::make_option<std::string>("original-file-pattern", "${id}_0cyc.pdb.gz", "Pattern for the original xyzin file"),
		mcfp::make_option<std::string>("final-file-pattern", "${id}_final.cif", "Pattern for the final xyzin file")
		);

	std::error_code ec;
	config.parse(argc, argv, ec);
	if (ec)
		throw std::runtime_error("Error parsing arguments: " + ec.message());

	if (config.has("version"))
	{
		write_version_string(std::cout, config.has("verbose"));
		exit(0);
	}

	if (config.has("help"))
	{
		std::cerr << config << std::endl;
		exit(config.has("help") ? 0 : 1);
	}

	config.parse_config_file("config", "prsmd.conf", { fs::current_path().string(), "/etc/" }, ec);
	if (ec)
		throw std::runtime_error("Error parsing config file: " + ec.message());

	// --------------------------------------------------------------------

	if (config.has("help") or config.operands().empty())
	{
		std::cerr << config << std::endl
				  << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options
			 )" << std::endl;
		exit(config.has("help") ? 0 : 1);
	}

	for (const char *option : { "pdb-redo-services-dir", "pdb-redo-db-dir", "pdb-redo-tools-dir" })
	{
		if (config.has(option))
			continue;
		std::cerr << "Missing " << option << " option" << std::endl;
		exit(1);
	}

	try
	{
		std::stringstream vConn;
		for (std::string opt : { "db-host", "db-port", "db-dbname", "db-user", "db-password" })
		{
			if (config.has(opt) == 0)
				continue;

			vConn << opt.substr(3) << "=" << config.get<std::string>(opt) << ' ';
		}

		prsm_db_connection::init(vConn.str());

		std::string admin = config.get<std::string>("admin");
		std::string pdbRedoServicesDir = config.get<std::string>("pdb-redo-services-dir");
		std::string runsDir = pdbRedoServicesDir + "/runs";
		if (config.has("runs-dir"))
			runsDir = config.get<std::string>("runs-dir");

		RunService::init(runsDir);

		SessionStore::init();
		UserService::init(admin);

		std::string secret;
		if (config.has("secret"))
			secret = config.get<std::string>("secret");
		else
		{
			secret = zeep::encode_base64(zeep::random_hash());
			std::cerr << "starting with created secret " << secret << std::endl;
		}

		std::string context;
		if (config.has("context"))
			context = config.get<std::string>("context");

		zh::daemon server([secret, context]()
			{
			auto sc = new zh::security_context(secret, UserService::instance());
			sc->add_rule("/admin", { "ADMIN" });
			sc->add_rule("/admin/**", { "ADMIN" });
			sc->add_rule("/job", { "USER" });
			sc->add_rule("/job/**", { "USER" });

			sc->add_rule("/{change-password,update-info,token}", { "USER" });

			sc->add_rule("/**", {});

			sc->register_password_encoder<PasswordEncoder>();
			sc->set_validate_csrf(true);

			auto s = new zeep::http::server(sc);

			auto access_control = new zeep::http::access_control("*", true);
			access_control->add_allowed_header("X-PDB-REDO-Date");
			access_control->add_allowed_header("Authorization");
			s->set_access_control(access_control);
	
			if (not context.empty())
				s->set_context_name(context);

			s->add_error_handler(new prsm_db_error_handler());

#ifndef NDEBUG
			s->set_template_processor(new zeep::http::file_based_html_template_processor("docroot"));
#else
			s->set_template_processor(new zeep::http::rsrc_based_html_template_processor());
#endif

			s->add_controller(new UserHTMLController());
			
			s->add_controller(new RootHTMLController());
			s->add_controller(new DBHTMLController());
			s->add_controller(new SessionRESTController());
			s->add_controller(new APIRESTController());

			s->add_controller(new GFXRESTController());

			s->add_controller(new JobHTMLController());

			return s; },
			kProjectName);

		std::string user = "www-data";
		if (config.has("user") != 0)
			user = config.get<std::string>("user");

		std::string address = "0.0.0.0";
		if (config.has("address"))
			address = config.get<std::string>("address");

		uint16_t port = 10339;
		if (config.has("port"))
			port = config.get<uint16_t>("port");

		std::string command = config.operands().front();

		if (command == "start")
		{
			if (address.find(':') != std::string::npos)
				std::cout << "starting server at http://[" << address << "]:" << port << '/' << std::endl;
			else
				std::cout << "starting server at http://" << address << ':' << port << '/' << std::endl;

			if (config.has("no-daemon"))
				result = server.run_foreground(address, port);
			else
				result = server.start(address, port, 2, 1, user);
		}
		else if (command == "stop")
			result = server.stop();
		else if (command == "status")
			result = server.status();
		else if (command == "reload")
			result = server.reload();
		else
		{
			std::cerr << "Invalid command" << std::endl;
			result = 1;
		}
	}
	catch (const std::exception &ex)
	{
		std::cerr << "exception:" << std::endl
				  << ex.what() << std::endl;
		result = 1;
	}

	return result;
}

// --------------------------------------------------------------------

// recursively print exception whats:
void print_what(const std::exception &e)
{
	std::cerr << e.what() << std::endl;
	try
	{
		std::rethrow_if_nested(e);
	}
	catch (const std::exception &nested)
	{
		std::cerr << " >> ";
		print_what(nested);
	}
}

// --------------------------------------------------------------------

int main(int argc, char *const argv[])
{
	int result = 0;

	try
	{
		result = a_main(argc, argv);
	}
	catch (const std::exception &ex)
	{
		print_what(ex);
		exit(1);
	}

	return result;
}
