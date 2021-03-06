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

#include <zeep/config.hpp>

#include <functional>
#include <tuple>
#include <thread>
#include <condition_variable>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/random/random_device.hpp>

#include <zeep/crypto.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/http/rest-controller.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/http/login-controller.hpp>
#include <zeep/http/uri.hpp>

#include <pqxx/pqxx>

#include "user-service.hpp"
#include "run-service.hpp"
#include "prsm-db-connection.hpp"

#include "mrsrc.h"

namespace zh = zeep::http;
namespace fs = std::filesystem;
namespace ba = boost::algorithm;
namespace po = boost::program_options;
namespace pt = boost::posix_time;

using json = zeep::json::element;

// --------------------------------------------------------------------

#define APP_NAME "prsmd"

// const std::string
// 	kPDB_REDO_Session_Realm("PDB-REDO Session Management"),
// 	kPDB_REDO_API_Realm("PDB-REDO API"),
// 	kPDB_REDO_API_Cookie("PDB-REDO-Signature"),
// 	kPDB_REDO_Cookie("PDB-REDO_Session");

fs::path gExePath;

// --------------------------------------------------------------------

struct Session
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string token;
	boost::posix_time::ptime created;
	boost::posix_time::ptime expires;

	Session& operator=(const pqxx::row& row)
	{
		id			= row.at("id").as<unsigned long>();
		name		= row.at("name").as<std::string>();
		user		= row.at("user").as<std::string>();
		token		= row.at("token").as<std::string>();
		created		= zeep::value_serializer<boost::posix_time::ptime>::from_string(row.at("created").as<std::string>());
		expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(row.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const	{ return id != 0; }

	bool expired() const	{ return pt::second_clock::universal_time() > expires; }

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("expires", expires);
	}
};

struct CreateSessionResult
{
	unsigned long id = 0;
	std::string name;
	std::string token;
	boost::posix_time::ptime expires;

	CreateSessionResult(const Session& session)
		: id(session.id)
		, name(session.name)
		, token(session.token)
		, expires(session.expires)
	{
	}

	CreateSessionResult(const pqxx::row& row)
		: id(row.at("id").as<unsigned long>())
		, name(row.at("name").as<std::string>())
		, token(row.at("token").as<std::string>())
		, expires(zeep::value_serializer<boost::posix_time::ptime>::from_string(row.at("expires").as<std::string>()))
	{
	}

	CreateSessionResult& operator=(const Session& session)
	{
		id = session.id;
		name = session.name;
		token = session.token;
		expires = session.expires;
		
		return *this;
	}

	CreateSessionResult& operator=(const pqxx::row& row)
	{
		id			= row.at("id").as<unsigned long>();
		name		= row.at("name").as<std::string>();
		token		= row.at("token").as<std::string>();
		expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(row.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const	{ return id != 0; }

	bool expired() const	{ return pt::second_clock::universal_time() > expires; }

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("expires", expires);
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

	static SessionStore& instance()
	{
		return *sInstance;
	}

	void stop()
	{
		delete sInstance;
		sInstance = nullptr;
	}

	Session create(const std::string& name, const std::string& user);

	Session get_by_id(unsigned long id);
	Session get_by_token(const std::string& token);
	void delete_by_id(unsigned long id);

	std::vector<Session> get_all_sessions();

  private:

	SessionStore();
	~SessionStore();

	SessionStore(const SessionStore&) = delete;
	SessionStore& operator=(const SessionStore&) = delete;

	void run_clean_thread();

	bool m_done = false;

	std::condition_variable m_cv;
	std::mutex m_cv_m;
	std::thread m_clean;

	static SessionStore* sInstance;
};

SessionStore* SessionStore::sInstance = nullptr;

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
			catch (const std::exception& ex)
			{
				std::cerr << ex.what() << std::endl;
			}
		}
	}
}

Session SessionStore::create(const std::string& name, const std::string& user)
{
	User u = UserService::instance().get_user(user);

	pqxx::transaction tx(prsm_db_connection::instance());

	std::string token = zeep::encode_base64url(zeep::random_hash());

	auto r = tx.exec1(
		R"(INSERT INTO session (user_id, name, token)
		   VALUES ()" + std::to_string(u.id) + ", " + tx.quote(name) + ", " + tx.quote(token) + R"()
		   RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	unsigned long tokenid = r[0].as<unsigned long>();
	std::string created = r[1].as<std::string>();

	tx.commit();
	
	return
	{
		tokenid,
		name,
		user,
		token,
		zeep::value_serializer<boost::posix_time::ptime>::from_string(created)
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
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.id = )" + std::to_string(id));

	Session result = {};
	if (r.size() == 1)
		result = r.front();

	tx.commit();
	
	return result;
}

Session SessionStore::get_by_token(const std::string& token)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec(
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.token = )" + tx.quote(token));

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
			 FROM session a, auth_user b
			WHERE a.user_id = b.id
			ORDER BY a.created ASC)");

	for (auto row: rows)
	{
		Session session;
		session = row;
		result.push_back(std::move(session));
	}

	return result;
}

// --------------------------------------------------------------------

class session_rest_controller : public zh::rest_controller
{
  public:
	session_rest_controller()
		: zh::rest_controller("api")
	{
		// create a new session, user should provide username, password and session name
		map_post_request("session", &session_rest_controller::post_session, "user", "password", "name");
	}

	// CRUD routines
	CreateSessionResult post_session(std::string user, std::string password, std::string name)
	{
		User u = UserService::instance().get_user(user);
		std::string pw = u.password;

		prsm_pw_encoder pwenc;

		if (not pwenc.matches(password, u.password))
			throw std::runtime_error("Invalid username/password");

		std::string token = zeep::encode_base64url(zeep::random_hash());
		unsigned long userid = u.id;

		pqxx::transaction tx(prsm_db_connection::instance());
		auto t = tx.exec1(
			R"(INSERT INTO session (user_id, name, token)
			   VALUES ()" + std::to_string(userid) + ", " + tx.quote(name) + ", " + tx.quote(token) + R"()
			RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
					  trim(both '"' from to_json(expires)::text) AS expires)");

		tx.commit();

		return t;
	}
};

class api_rest_controller : public zh::rest_controller
{
  public:
	api_rest_controller(const std::string& pdbRedoDir)
		: zh::rest_controller("api")
		, m_pdb_redo_dir(pdbRedoDir)
	{
		// get session info
		map_get_request("session/{id}", &api_rest_controller::get_session, "id");

		// delete a session
		map_delete_request("session/{id}", &api_rest_controller::delete_session, "id");

		// return a list of runs
		map_get_request("session/{id}/run", &api_rest_controller::get_all_runs, "id");

		// Submit a run (job)
		map_post_request("session/{id}/run", &api_rest_controller::create_job, "id",
			"mtz-file", "pdb-file", "restraints-file", "sequence-file", "parameters");

		// return info for a run
		map_get_request("session/{id}/run/{run}", &api_rest_controller::get_run, "id", "run");

		// get a list of the files in output
		map_get_request("session/{id}/run/{run}/output", &api_rest_controller::get_result_file_list, "id", "run");

		// get a result file
		map_get_request("session/{id}/run/{run}/output/{file}", &api_rest_controller::get_result_file, "id", "run", "file");
	}

	virtual bool handle_request(zh::request& req, zh::reply& rep)
	{
		bool result = false;

		try
		{
			std::string authorization = req.get_header("Authorization");
			// PDB-REDO-api Credential=token-id/date/pdb-redo-apiv2,SignedHeaders=host;x-pdb-redo-content-sha256,Signature=xxxxx

			if (not ba::starts_with(authorization, "PDB-REDO-api "))
				throw zh::unauthorized_exception();

			std::vector<std::string> signedHeaders;

			std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

			std::smatch m;
			if (not std::regex_search(authorization, m, re))
				throw zh::unauthorized_exception();

			std::vector<std::string> credentials;
			std::string credential = m[1].str();
			ba::split(credentials, credential, ba::is_any_of("/"));

			if (credentials.size() != 3 or credentials[2] != "pdb-redo-api")
				throw zh::unauthorized_exception();

			auto signature = zeep::decode_base64(m[3].str());

			// Validate the signature

			// canonical request

			std::vector<std::tuple<std::string,std::string>> params;
			for (auto& p: req.get_parameters())
				params.push_back(std::make_tuple(p.first, p.second));
			std::sort(params.begin(), params.end());
			std::ostringstream ps;
			auto n = params.size();
			for (auto& [name, value]: params)
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
		catch (const std::exception& e)
		{
			using namespace std::literals;

			rep.set_content(json({
				{ "error", e.what() }
			}));
			rep.set_status(zh::unauthorized);

			result = true;
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

	Run create_job(unsigned long sessionID, const zh::file_param& diffractionData, const zh::file_param& coordinates,
		const zh::file_param& restraints, const zh::file_param& sequence, const json& params)
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

	fs::path get_result_file(unsigned long sessionID, unsigned long runID, const std::string& file)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().get_result_file(session.user, runID, file);
	}

  private:
	fs::path m_pdb_redo_dir;
};

// --------------------------------------------------------------------

class prsm_html_controller : public zh::html_controller
{
  public:
	prsm_html_controller(const std::string& pdbRedoDir)
		: m_pdb_redo_dir(pdbRedoDir)
	{
		mount("", &prsm_html_controller::welcome);

		mount("admin", &prsm_html_controller::admin);
		mount("admin/deleteSession", "DELETE", &prsm_html_controller::handle_delete_session);
		mount("{css,scripts,fonts,images}/", &prsm_html_controller::handle_file);
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void admin(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	void handle_delete_session(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	std::string m_pdb_redo_dir;
};

void prsm_html_controller::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	get_template_processor().create_reply_from_template("index.html", scope, reply);
}

void prsm_html_controller::admin(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	json sessions;
	auto s = SessionStore::instance().get_all_sessions();
	to_element(sessions, s);
	sub.put("sessions", sessions);

	get_template_processor().create_reply_from_template("admin.html", sub, reply);
}

void prsm_html_controller::handle_delete_session(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	unsigned long sessionID = std::stoul(request.get_parameter("sessionid", "0"));
	if (sessionID != 0)
		SessionStore::instance().delete_by_id(sessionID);

	reply = zh::reply::stock_reply(zh::ok);
}

// --------------------------------------------------------------------

int main(int argc, const char* argv[])
{
	using namespace std::literals;

	int result = 0;

	po::options_description visible(argv[0] + " [options] command"s);
	visible.add_options()
		("help,h",										"Display help message")
		("verbose,v",									"Verbose output")
		("no-daemon,F",									"Do not fork into background")
		("config",		po::value<std::string>(),		"Specify the config file to use")
		;
	
	po::options_description config(APP_NAME R"( config file options)");
	
	config.add_options()
		("pdb-redo-dir",	po::value<std::string>(),	"Directory containing PDB-REDO server data")
		("runs-dir",		po::value<std::string>(),	"Directory containing PDB-REDO server run directories")
		("address",			po::value<std::string>(),	"External address, default is 0.0.0.0")
		("port",			po::value<uint16_t>(),		"Port to listen to, default is 10339")
		("user,u",			po::value<std::string>(),	"User to run the daemon")
		("db-host",			po::value<std::string>(),	"Database host")
		("db-port",			po::value<std::string>(),	"Database port")
		("db-dbname",		po::value<std::string>(),	"Database name")
		("db-user",			po::value<std::string>(),	"Database user name")
		("db-password",		po::value<std::string>(),	"Database password")
		("admin",			po::value<std::string>(),	"Administrators, list of usernames separated by comma")
		("secret",			po::value<std::string>(),	"Secret value, used in signing access tokens");

	po::options_description hidden("hidden options");
	hidden.add_options()
		("command",			po::value<std::string>(),	"Command, one of start, stop, status or reload")
		("debug,d",			po::value<int>(),			"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	p.add("command", 1);

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	fs::path configFile = APP_NAME ".conf";

	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / APP_NAME ".conf";

	if (not fs::exists(configFile) and fs::exists("/etc" / configFile))
		configFile = "/etc" / configFile;
	
	if (vm.count("config") != 0)
	{
		configFile = vm["config"].as<std::string>();
		if (not fs::exists(configFile))
			throw std::runtime_error("Specified config file does not seem to exist");
	}
	
	if (fs::exists(configFile))
	{
		po::options_description config_options ;
		config_options.add(config).add(hidden);

		std::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, config_options), vm);
	}
	
	po::notify(vm);

	// --------------------------------------------------------------------

	if (vm.count("help") or vm.count("command") == 0)
	{
		std::cerr << visible << std::endl
			 << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options
			 )" << std::endl;
		exit(vm.count("help") ? 0 : 1);
	}
	
	if (vm.count("pdb-redo-dir") == 0)
	{
		std::cerr << "Missing pdb-redo-dir option" << std::endl;
		exit(1);
	}

	try
	{
		char exePath[PATH_MAX + 1];
		int r = readlink("/proc/self/exe", exePath, PATH_MAX);
		if (r > 0)
		{
			exePath[r] = 0;
			gExePath = fs::weakly_canonical(exePath);
		}
		
		if (not fs::exists(gExePath))
			gExePath = fs::weakly_canonical(argv[0]);

		std::vector<std::string> vConn;
		for (std::string opt: { "db-host", "db-port", "db-dbname", "db-user", "db-password" })
		{
			if (vm.count(opt) == 0)
				continue;
			
			vConn.push_back(opt.substr(3) + "=" + vm[opt].as<std::string>());
		}

		prsm_db_connection::init(ba::join(vConn, " "));

		std::string admin = vm["admin"].as<std::string>();
		std::string pdbRedoDir = vm["pdb-redo-dir"].as<std::string>();
		std::string runsDir = pdbRedoDir + "/runs";
		if (vm.count("runs-dir"))
			runsDir = vm["runs-dir"].as<std::string>();

		RunService::init(runsDir);

		SessionStore::init();
		UserService::init(admin);
		
		std::string secret;
		if (vm.count("secret"))
			secret = vm["secret"].as<std::string>();
		else
		{
			secret = zeep::encode_base64(zeep::random_hash());
			std::cerr << "starting with created secret " << secret << std::endl;
		}

		zh::daemon server([secret, pdbRedoDir]()
		{
			auto sc = new zh::security_context(secret, UserService::instance());
			sc->add_rule("/admin", { "ADMIN" });
			sc->add_rule("/admin/**", { "ADMIN" });
			sc->add_rule("/", {});

			sc->register_password_encoder<prsm_pw_encoder>();

			auto s = new zeep::http::server(sc);

			s->add_error_handler(new prsm_db_error_handler());

#if DEBUG
			s->set_template_processor(new zeep::http::file_based_html_template_processor("docroot"));
#else
			s->set_template_processor(new zeep::http::rsrc_based_html_template_processor());
#endif

			s->add_controller(new zh::login_controller());

			s->add_controller(new prsm_html_controller(pdbRedoDir));
			s->add_controller(new session_rest_controller());
			s->add_controller(new api_rest_controller(pdbRedoDir));

			return s;
		}, APP_NAME );

		std::string user = "www-data";
		if (vm.count("user") != 0)
			user = vm["user"].as<std::string>();
		
		std::string address = "0.0.0.0";
		if (vm.count("address"))
			address = vm["address"].as<std::string>();

		uint16_t port = 10339;
		if (vm.count("port"))
			port = vm["port"].as<uint16_t>();

		std::string command = vm["command"].as<std::string>();

		if (command == "start")
		{
			if (address.find(':') != std::string::npos)
				std::cout << "starting server at http://[" << address << "]:" << port << '/' << std::endl;
			else
				std::cout << "starting server at http://" << address << ':' << port << '/' << std::endl;

			if (vm.count("no-daemon"))
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