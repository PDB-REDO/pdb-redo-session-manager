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

#include "api-controller.hpp"
#include "data-service.hpp"
#include "prsm-db-connection.hpp"
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

json create_entry_data(json &data, const fs::path &dir, const std::vector<std::string> &files)
{
	auto pdbID = data["pdbid"].as<std::string>();

	zeep::json::element entry{
		{ "id", data["pdbid"] },
		{ "dbEntry", false },
		{ "data", std::move(data["properties"]) },
		{ "rama-angles", std::move(data["rama-angles"]) }
	};

	auto &link = entry["link"];
	for (fs::path file : files)
	{
		if (file.empty() or file.begin()->string() == "attic")
			continue;

		if (zeep::ends_with(file.string(), "final.pdb"))
			link["final_pdb"] = dir / file;
		else if (zeep::ends_with(file.string(), "final.cif") or zeep::ends_with(file.string(), "final.cif.gz"))
			link["final_cif"] = dir / file;
		else if (zeep::ends_with(file.string(), "final.mtz") or zeep::ends_with(file.string(), "final.mtz.gz"))
			link["final_mtz"] = dir / file;
		else if (zeep::ends_with(file.string(), "besttls.pdb.gz"))
			link["besttls_pdb"] = dir / file;
		else if (zeep::ends_with(file.string(), "besttls.mtz.gz"))
			link["besttls_mtz"] = dir / file;
		else if (zeep::ends_with(file.string(), ".refmac"))
			link["refmac_settings"] = dir / file;
		else if (zeep::ends_with(file.string(), "homology.rest"))
			link["homology_rest"] = dir / file;
		else if (zeep::ends_with(file.string(), "hbond.rest"))
			link["hbond_rest"] = dir / file;
		else if (zeep::ends_with(file.string(), "metal.rest"))
			link["metal_rest"] = dir / file;
		else if (zeep::ends_with(file.string(), "nucleic.rest"))
			link["nucleic_rest"] = dir / file;
		else if (zeep::ends_with(file.string(), "wo/pdbout.txt"))
			link["wo"] = dir / file;
		else if (zeep::ends_with(file.string(), "wf/pdbout.txt"))
			link["wf"] = dir / file;
		else if (file == pdbID + ".log")
			link["log"] = dir / file;
	}

	link["alldata"] = dir / "zipped";

	return entry;
}

json create_entry_data(Run &run, const fs::path &basePath)
{
	auto dataJsonFile = run.getResultFile("data.json");
	std::ifstream dataJson(dataJsonFile);

	if (not dataJson.is_open())
		throw zeep::http::not_found;

	zeep::json::element data;
	zeep::json::parse_json(dataJson, data);

	return create_entry_data(data, basePath, run.getResultFileList());
}

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

// --------------------------------------------------------------------

class JobController : public zh::html_controller
{
  public:
	JobController()
		: zh::html_controller("job")
	{
		map_get("", &JobController::getJobListing);
		map_post("", &JobController::postJob, "mtz", "coords", "restraints", "sequence", "params");

		map_get("output/{job-id}/{file}", &JobController::getOutputFile, "job-id", "file");
		map_get("image/{job-id}", &JobController::getImageFile, "job-id");
		map_get("result/{job-id}", &JobController::getResult, "job-id");
		map_get("entry/{job-id}", &JobController::getEntry, "job-id");
		map_delete("{job-id}", &JobController::deleteJob, "job-id");

		map_get("status", &JobController::getStatus, "ids");
	}

	zh::reply getJobListing(const zh::scope &scope)
	{
		auto credentials = scope.get_credentials();

		zh::scope sub(scope);

		sub.put("page", "job");

		std::error_code ec;
		json runs;

		for (auto &run : RunService::instance().getRunsForUser(credentials["username"].as<std::string>()))
		{
			json run_j;
			to_element(run_j, run);
			runs.emplace_back(std::move(run_j));
		}
		sub.put("runs", std::move(runs));

		return get_template_processor().create_reply_from_template("jobs", sub);
	}

	zh::reply postJob(const zh::scope &scope, const zh::file_param &diffractionData, const zh::file_param &coordinates,
		const zh::file_param &restraints, const zh::file_param &sequence, bool pairedRefinement)
	{
		auto credentials = scope.get_credentials();

		zeep::json::element params;
		if (pairedRefinement)
			params["paired"] = true;

		auto r = RunService::instance().submit(credentials["username"].as<std::string>(), coordinates, diffractionData, restraints, sequence, params);

		return zh::reply::redirect("/job", zh::see_other);
	}

	zh::reply getOutputFile(const zh::scope &scope, unsigned long job_id, const std::string &file)
	{
		auto credentials = scope.get_credentials();
		auto run = RunService::instance().getRun(credentials["username"].as<std::string>(), job_id);

		zh::reply result(zh::ok);

		if (file == "zipped")
		{
			auto [f, name] = run.getZippedResultFile();
			result.set_content(f, "application/zip");
			result.set_header("content-disposition", "attachement; filename = \"" + name + "\"");
		}
		else
		{
			auto f = run.getResultFile(file);

			std::error_code ec;
			if (not fs::exists(f, ec))
				return zh::reply::stock_reply(zh::not_found);
			
			result.set_content(new std::ifstream(f), "application/octet-stream");
			result.set_header("content-disposition", "attachement; filename = \"" + f.filename().string() + "\"");
		}

		return result;
	}

	zh::reply getImageFile(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();

		auto f = RunService::instance().getRun(credentials["username"].as<std::string>(), job_id).getImageFile();

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f, std::ios::in | std::ios::binary), "image/png");
		return result;
	}

	zh::reply getResult(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();

		auto r = RunService::instance().getRun(credentials["username"].as<std::string>(), job_id);

		zh::scope sub(scope);

		sub.put("job-id", job_id);

		if (r.status == RunStatus::ENDED)
		{
			auto entry = create_entry_data(r, "job/output/" + std::to_string(job_id));

			zh::scope sub(scope);
			sub.put("entry", entry);

			return get_template_processor().create_reply_from_template("job-result", sub);
		}

		auto f = r.getResultFile("process.log");
		std::ifstream in(f);
		std::stringstream content;
		content << in.rdbuf();

		sub.put("log", content.str());
		sub.put("status", r.status);

		return get_template_processor().create_reply_from_template("job-error", sub);
	}

	zh::reply getEntry(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();
		auto r = RunService::instance().getRun(credentials["username"].as<std::string>(), job_id);

		auto entry = create_entry_data(r, "job/output/" + std::to_string(job_id));

		zh::scope sub(scope);
		sub.put("entry", entry);

		return get_template_processor().create_reply_from_template("entry::tables", sub);
	}

	zh::reply deleteJob(const zh::scope &scope, unsigned long job_id)
	{
		auto credentials = scope.get_credentials();
		RunService::instance().deleteRun(credentials["username"].as<std::string>(), job_id);

		return zh::reply::stock_reply(zh::ok);
	}

	zh::reply getStatus(const zh::scope &scope, std::vector<unsigned long> job_ids)
	{
		auto credentials = scope.get_credentials();
		auto username = credentials["username"].as<std::string>();

		zeep::json::element status;
		for (auto job_id : job_ids)
		{
			auto r = RunService::instance().getRun(username, job_id);
			status.emplace_back(r.status);
		}

		zh::reply reply(zh::ok);
		reply.set_content(status);
		return reply;
	}
};

// --------------------------------------------------------------------

class RootController : public zh::html_controller
{
  public:
	RootController(fs::path pdb_db_dir)
		: m_db_dir(pdb_db_dir)
	{
		map_get("", "index");
		map_get("about", "about");
		map_get("privacy-policy", "gdpr");
		map_get("download", "download");
		map_get("license", "license");
		map_get("api-doc", "api-doc");

		mount("client-api/**", &RootController::handle_client_api_file);

		mount("{css,scripts,fonts,images}/", &RootController::handle_file);

		mount("{others,schema}/**", &RootController::handle_others);

		map_post("entry", &RootController::handle_entry, "data.json", "link-url");

		map_get("nextUpdateRequest", &RootController::nextUpdateRequest);
	}

	// zh::reply handle_entry(const zh::scope &scope, const std::string &tokenID, const std::string &tokenSecret, const std::string &jobID);
	zh::reply handle_entry(const zh::scope &scope, const zeep::json::element &data, const std::optional<std::string> &link_url);

	// For the 'others' directory
	void handle_others(const zh::request& request, const zh::scope& scope, zh::reply& reply)
	{
		m_db_dir.handle_file(request, scope, reply);
	}

	void handle_client_api_file(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	zh::reply nextUpdateRequest(const zh::scope &scope);

  private:
	zh::file_based_html_template_processor m_db_dir;
};

zh::reply RootController::handle_entry(const zh::scope &scope, const zeep::json::element &data, const std::optional<std::string> &data_link)
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
		std::string db = *data_link + "/";

		link["final_pdb"] = db + (pdbID + "_final.pdb");
		link["final_cif"] = db + (pdbID + "_final.cif");
		link["final_mtz"] = db + (pdbID + "_final.mtz");
		link["besttls_pdb"] = db + (pdbID + "_besttls.pdb.gz");
		link["besttls_mtz"] = db + (pdbID + "_besttls.mtz.gz");
		link["refmac_settings"] = db + (pdbID + ".refmac");
		link["homology_rest"] = db + "homology.rest";
		link["hbond_rest"] = db + "hbond.rest";
		link["metal_rest"] = db + "metal.rest";
		link["nucleic_rest"] = db + "nucleic.rest";
		link["wo"] = db + "wo/pdbout.txt";
		link["wf"] = db + "wf/pdbout.txt";

		link["alldata"] = db + "zipped";
	}

	zh::scope sub(scope);
	sub.put("entry", entry);

	return get_template_processor().create_reply_from_template("entry::tables", sub);
}

void RootController::handle_client_api_file(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	fs::path file = fs::path(scope["baseuri"].as<std::string>()).lexically_relative("client-api");

	mrsrc::rsrc data(file.string());

	if (not data)
		throw zh::not_found;
	
	reply = zh::reply::stock_reply(zh::ok);
	reply.set_content(new mrsrc::istream(data), "text/plain");
}

zh::reply RootController::nextUpdateRequest(const zh::scope &scope)
{
	std::ostringstream os;

	for (auto ur : DataService::instance().getAllUpdateRequests())
		os << ur.pdb_id << std::endl;

	zh::reply result(zh::ok);
	result.set_content(os.str(), "text/plain");
	return result;
}

// --------------------------------------------------------------------

class AdminController : public zh::html_controller
{
  public:
	AdminController()
		: zh::html_controller("admin")
	{
		map_get("", &AdminController::admin, "tab");
		map_get("job/{user}/{id}/output/{file}", &AdminController::handle_get_job_file, "user", "id", "file");
		map_get("job/{user}/{id}", &AdminController::job, "user", "id");
		map_get("delete/jobs/{user}/{id}", &AdminController::handle_delete_job, "user", "id");
		map_get("delete/{tab}/{id}", &AdminController::handle_delete, "tab", "id");
	}

	zh::reply admin(const zh::scope &scope, std::optional<std::string> tab);
	zh::reply job(const zh::scope &scope, const std::string &user, unsigned long id);
	zh::reply handle_get_job_file(const zh::scope &scope, const std::string &user, unsigned long id, const std::string &file);

	zh::reply handle_delete(const zh::scope &scope, const std::string &tab, unsigned long id);
	zh::reply handle_delete_job(const zh::scope &scope, const std::string &user, unsigned long id);
};

zh::reply AdminController::admin(const zh::scope &scope, std::optional<std::string> tab)
{
	zh::scope sub(scope);

	sub.put("page", "admin");

	std::string active = tab.value_or("users");
	sub.put("tab", active);

	if (active == "tokens")
	{
		json tokens;
		auto s = TokenService::instance().getAllTokens();
		to_element(tokens, s);
		sub.put("tokens", tokens);
	}
	else if (active == "users")
	{
		json users;
		auto u = UserService::instance().getAllUsers();
		to_element(users, u);
		sub.put("users", users);
	}
	else if (active == "jobs")
	{
		json runs;
		auto r = RunService::instance().getAllRuns();
		to_element(runs, r);
		sub.put("runs", runs);
	}
	else if (active == "updates")
	{
		json updates;
		auto ur = DataService::instance().getAllUpdateRequests();
		to_element(updates, ur);
		sub.put("updates", updates);
	}

	return get_template_processor().create_reply_from_template("admin", sub);
}

zh::reply AdminController::job(const zh::scope &scope, const std::string &user, unsigned long job_id)
{
	auto run = RunService::instance().getRun(user, job_id);

	if (run.status == RunStatus::ENDED)
	{
		auto entry = create_entry_data(run, "admin/job/" + user + '/' + std::to_string(job_id) + "/output/");

		zh::scope sub(scope);
		sub.put("entry", entry);

		return get_template_processor().create_reply_from_template("admin-job-result", sub);
	}

	auto f = run.getResultFile("process.log");

	std::error_code ec;
	if (fs::exists(f, ec))
	{
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f), "text/plain");
		return result;
	}

	return zh::reply::stock_reply(zh::not_found);
}

zh::reply AdminController::handle_get_job_file(const zh::scope &scope, const std::string &user, unsigned long job_id, const std::string &file)
{
	auto run = RunService::instance().getRun(user, job_id);

	zh::reply result(zh::ok);

	if (file == "zipped")
	{
		auto [f, name] = run.getZippedResultFile();
		result.set_content(f, "application/zip");
		result.set_header("content-disposition", "attachement; filename = \"" + name + "\"");
	}
	else
	{
		auto f = run.getResultFile(file);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		result.set_content(new std::ifstream(f), "application/octet-stream");
		result.set_header("content-disposition", "attachement; filename = \"" + f.filename().string() + "\"");
	}

	return result;
}

zh::reply AdminController::handle_delete(const zh::scope &scope, const std::string &tab, unsigned long id)
{
	if (tab == "users")
	{
		auto &user_service = UserService::instance();
		
		auto me = user_service.getUser(scope.get_credentials()["username"].as<std::string>());
		if (me.id == id)
			throw std::runtime_error("Are you serious, do you want to throw away yourself?");

		user_service.deleteUser(id);
	}
	// else if (tab == "jobs")
	// 	RunService::instance().deleteRun();
	else if (tab == "tokens")
		TokenService::instance().deleteToken(id);
	else if (tab == "updates")
		DataService::instance().deleteUpdateRequest(id);

	return zh::reply::redirect("/admin?tab=" + tab);
}

zh::reply AdminController::handle_delete_job(const zh::scope &scope, const std::string &user, unsigned long id)
{
	RunService::instance().deleteRun(user, id);
	return zh::reply::redirect("/admin?tab=jobs");
}

// --------------------------------------------------------------------

class DbController : public zh::html_controller
{
  public:
	DbController()
		: zh::html_controller("db")
	{
		map_post("get", &DbController::handle_get, "pdb-id");

		map_get("entry", &DbController::handle_entry, "pdb-id", "attic");
		map_post("entry", &DbController::handle_entry, "pdb-id", "attic");

		map_get("update/{id}", &DbController::handle_update, "id");

		map_get("{id}/zipped", &DbController::handle_zipped, "id");
		map_get("{id}/{file}", &DbController::handle_file, "id", "file");

		map_get("{id}/attic/{attic}/zipped", &DbController::handle_zipped_attic, "id", "attic");
		map_get("{id}/attic/{attic}/{file}", &DbController::handle_file_attic, "id", "file", "attic");

		map_get("{id}", &DbController::handle_show, "id");
	}

	zh::reply handle_get(const zh::scope &scope, std::string pdbID);
	zh::reply handle_entry(const zh::scope &scope, std::string pdbID, std::optional<std::string> attic);
	zh::reply handle_show(const zh::scope &scope, std::string pdbID);

	zh::reply handle_update(const zh::scope &scope, std::string pdbID)
	{
		zeep::to_lower(pdbID);

		try
		{
			auto credentials = scope.get_credentials();
			if (not credentials)
				throw std::runtime_error("You cannot request an update for this PDB-REDO entry since you are not logged in");
			
			User user = UserService::instance().getUser(credentials["username"].as<std::string>());
			DataService::instance().requestUpdate(pdbID, user);

			return zh::reply::redirect("/db/" + pdbID);
		}
		catch (...)
		{
			std::throw_with_nested(std::runtime_error("The request for updating failed, did you already request an update before for this entry?"));
		}
	}

	zh::reply handle_zipped(const zh::scope &scope, std::string pdbID)
	{
		zeep::to_lower(pdbID);

		const auto &[is, name] = DataService::instance().getZipFile(pdbID);

		zh::reply rep{ zh::ok };
		rep.set_content(is, "application/zip");
		rep.set_header("content-disposition", "attachement; filename = \"" + name + '"');

		return rep;
	}

	zh::reply handle_file(const zh::scope &scope, std::string pdbID, std::string file)
	{
		zeep::to_lower(pdbID);

		auto f = DataService::instance().getFile(pdbID, file);

		std::error_code ec;
		if (not fs::exists(f, ec))
		{
			zeep::to_lower(file);
			f = DataService::instance().getFile(pdbID, file);
		}

		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f), "application/octet-stream");
		result.set_header("content-disposition", "attachement; filename = \"" + f.filename().string() + "\"");
		return result;
	}

	zh::reply handle_zipped_attic(const zh::scope &scope, std::string pdbID, const std::string &attic)
	{
		zeep::to_lower(pdbID);

		const auto &[is, name] = DataService::instance().getZipFile(pdbID, attic);

		zh::reply rep{ zh::ok };
		rep.set_content(is, "application/zip");
		rep.set_header("content-disposition", "attachement; filename = \"" + name + '"');

		return rep;
	}

	zh::reply handle_file_attic(const zh::scope &scope, std::string pdbID, const std::string &file, const std::string &attic)
	{
		zeep::to_lower(pdbID);

		auto f = DataService::instance().getFile(pdbID, file, attic);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zh::reply::stock_reply(zh::not_found);
		
		zh::reply result(zh::ok);
		result.set_content(new std::ifstream(f), "application/octet-stream");
		result.set_header("content-disposition", "attachement; filename = \"" + f.filename().string() + "\"");
		return result;
	}
};

zh::reply DbController::handle_get(const zh::scope &scope, std::string pdbID)
{
	if (pdbID.empty())
		throw std::runtime_error("Please specify a valid PDB ID");
	
	zeep::to_lower(pdbID);
	return zh::reply::redirect(pdbID, zh::see_other);
}

zh::reply DbController::handle_show(const zh::scope &scope, std::string pdbID)
{
	auto &ds = DataService::instance();

	zeep::to_lower(pdbID);

	zh::scope sub(scope);

	auto pdbRedoVersion = ds.version();

	sub.put("pdb-id", pdbID);
	sub.put("version", pdbRedoVersion);

	try
	{
		auto data = ds.getData(pdbID);
		if (data)
		{
			auto entry = create_entry_data(data, "db/" + pdbID, ds.getFileList(pdbID));

			entry["id"] = pdbID;
			entry["dbEntry"] = true;

			auto status = ds.getUpdateStatus(pdbID);
			to_element(entry["status"], status);

			sub.put("entry", entry);

			return get_template_processor().create_reply_from_template("db-entry", sub);
		}
	}
	catch (...) {}

	auto attic = ds.getLatestAttic(pdbID);
	if (not attic.empty())
	{
		try
		{
			auto data = ds.getData(pdbID, attic);
			auto entry = create_entry_data(data, "db/" + pdbID + "/attic/" + attic + '/', ds.getFileList(pdbID, attic));

			entry["id"] = pdbID;
			entry["dbEntry"] = true;

			auto status = ds.getUpdateStatus(pdbID);
			to_element(entry["status"], status);

			sub.put("entry", entry);
			sub.put("attic", attic);

			return get_template_processor().create_reply_from_template("db-entry", sub);
		}
		catch (...) {}
	}

	// OK, that failed. Find out whynot

	auto whynot = ds.getWhyNot(pdbID);
	sub.put("whynot", whynot);

	return get_template_processor().create_reply_from_template("why-not", sub);
}

zh::reply DbController::handle_entry(const zh::scope &scope, std::string pdbID, std::optional<std::string> attic)
{
	zeep::to_lower(pdbID);

	auto dataJsonFile = DataService::instance().getFile(pdbID, "data.json", attic);
	std::ifstream dataJson(dataJsonFile);

	zeep::json::element data;
	zeep::json::parse_json(dataJson, data);

	auto entry = create_entry_data(data, "db/" + pdbID, DataService::instance().getFileList(pdbID));

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

		mcfp::make_option<std::string>("ebi-coord-template", "https://www.ebi.ac.uk/pdbe/entry-files/download/pdb${id}.ent", "Link template for coord file at the EBI"),
		mcfp::make_option<std::string>("ebi-sf-template", "https://www.ebi.ac.uk/pdbe/entry-files/download/r${id}sf.ent", "Link template for sf file at the EBI"),

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

		TokenService::init();
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

		zh::daemon server([secret, context, &config]()
			{
			auto sc = new zh::security_context(secret, UserService::instance());
			sc->add_rule("/admin", { "ADMIN" });
			sc->add_rule("/admin/**", { "ADMIN" });
			sc->add_rule("/{job,tokens}", { "USER" });
			sc->add_rule("/{job,others}/**", { "USER" });

			sc->add_rule("/{change-password,update-info,token,delete,ccp4-token-request}", { "USER" });

			sc->add_rule("/**", {});

			sc->register_password_encoder<PasswordEncoder>();
			sc->set_validate_csrf(true);
			sc->set_jwt_exp(date::days{1});

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

			s->add_controller(new RootController(config.get("pdb-redo-db-dir")));
			s->add_controller(new UserHTMLController());
			s->add_controller(new AdminController());
			s->add_controller(new DbController());
			// s->add_controller(new TokenRESTController());
			s->add_controller(new APIRESTController_v1());
			s->add_controller(new APIRESTController_v2());

			s->add_controller(new GFXRESTController());

			s->add_controller(new JobController());

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
				result = server.start(address, port, 8, 8, user);
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
