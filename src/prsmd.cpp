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
#include "mrsrc.hpp"
#include "prsm-db-connection.hpp"
#include "revision.hpp"
#include "user-service.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <iterator>
#include <mcfp/mcfp.hpp>
#include <pqxx/pqxx>
#include <system_error>
#include <tuple>
#include <utility>
#include <zeep/crypto.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/http/login-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/http/status.hpp>
#include <zeep/uri.hpp>

namespace fs = std::filesystem;

using json = zeep::el::object;

// --------------------------------------------------------------------

class entry_class_expression_object : public zeep::http::expression_utility_object<entry_class_expression_object>
{
  public:
	static constexpr const char *name() { return "entry"; }

  protected:
	[[nodiscard]] zeep::http::object evaluate(const zeep::http::scope
												  & /*scope*/,
		const std::string &methodName,
		const std::vector<zeep::http::object> &parameters) const override
	{
		zeep::http::object result;

		try
		{
			if (methodName == "class" and parameters.size() == 2)
			{
				result = "";

				if (parameters.front().is_number() and parameters.back().is_number())
				{
					double o = parameters.front().get<double>();
					double f = parameters.back().get<double>();

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
					double o = parameters.front().get<double>();
					double f = parameters.back().get<double>();

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
					auto rffin = data["RFFIN"].get<double>();
					auto sigrfcal = data["SIGRFCAL"].get<double>();

					if (data["RFREE"].is_null() or data["ZCALERR"] == true or data["TSTCNT"] != data["NTSTCNT"])
					{
						if (data["RFCALUNB"].is_number())
						{
							auto rfcalunb = data["RFCALUNB"].get<double>();

							if (rffin < (rfcalunb - 2.6 * sigrfcal))
								result = "better";
							else if (rffin > (rfcalunb + 2.6 * sigrfcal))
								result = "worse";
						}
					}
					else if (data["RFCAL"].is_number())
					{
						double rfcal = data["RFCAL"].get<double>();

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
			std::cerr << ex.what() << '\n';
		}

		return result;
	}

} s_entry_class_expression_object;

// --------------------------------------------------------------------

class version_format_expression_object : public zeep::http::expression_utility_object<version_format_expression_object>
{
  public:
	static constexpr const char *name() { return "version"; }

  protected:
	[[nodiscard]] zeep::http::object evaluate(const zeep::http::scope
												  & /*scope*/,
		const std::string &methodName,
		const std::vector<zeep::http::object> &parameters) const override
	{
		zeep::http::object result;

		try
		{
			if (methodName == "format" and parameters.size() == 1 and parameters[0].is_number())
			{
				double d;
				if (parameters[0].is_number_int())
					d = static_cast<double>(parameters[0].get<int64_t>());
				else
					d = parameters[0].get<double>();

#if __cpp_lib_to_chars >= 201611L
				char b[32];

				auto r = std::to_chars(b, b + sizeof(b), d, std::chars_format::fixed, 2);
				if (r.ec == std::errc())
					result = std::string{ b, r.ptr };
				else
					result = std::make_error_code(r.ec).message();
#else
				std::ostringstream os;
				os << std::fixed << std::setprecision(2) << d;
				result = os.str();
#endif
			}
		}
		catch (const std::exception &ex)
		{
			std::cerr << ex.what() << '\n';
		}

		return result;
	}

} s_version_format_expression_object;

// --------------------------------------------------------------------

json create_entry_data(json &data, const fs::path &dir, const std::vector<std::string> &files)
{
	auto pdbID = data["pdbid"].get<std::string>();

	zeep::el::object entry{
		{ "id", data["pdbid"] },
		{ "dbEntry", false },
		{ "data", std::move(data["properties"]) },
		{ "versions", std::move(data["_versions"]) },
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
		else if (zeep::ends_with(file.string(), "besttls.cif.gz"))
			link["besttls_cif"] = dir / file;
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
		throw std::system_error(zeep::http::status_type::not_found);

	zeep::el::object data = zeep::el::object::parse_JSON(dataJson);

	return create_entry_data(data, basePath, run.getResultFileList());
}

// --------------------------------------------------------------------

struct Stats
{
	double RFREE, RFFIN, OZRAMA, FZRAMA, OCHI12, FCHI12, URESO;

	Stats() = default;
	Stats(double RFREE, double RFFIN, double OZRAMA, double FZRAMA, double OCHI12, double FCHI12, double URESO)
		: RFREE(RFREE)
		, RFFIN(RFFIN)
		, OZRAMA(OZRAMA)
		, FZRAMA(FZRAMA)
		, OCHI12(OCHI12)
		, FCHI12(FCHI12)
		, URESO(URESO)
	{
	}

	Stats(const Stats &) = default;
	Stats &operator=(const Stats &) = default;

	auto operator<=>(const Stats &rhs) const noexcept
	{
		return URESO <=> rhs.URESO;
	}

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("RFREE", RFREE)
		   & zeem::name_value_pair("RFFIN", RFFIN)
		   & zeem::name_value_pair("OZRAMA", OZRAMA)
		   & zeem::name_value_pair("FZRAMA", FZRAMA)
		   & zeem::name_value_pair("OCHI12", OCHI12)
		   & zeem::name_value_pair("FCHI12", FCHI12)
		   & zeem::name_value_pair("URESO", URESO);
	}
};

class GFXRESTController : public zeep::http::controller
{
  public:
	GFXRESTController()
		: zeep::http::controller("gfx")
	{
		map_get_request("statistics-for-box-plot", &GFXRESTController::get_statistics_for_box_plot, "ureso");

		auto &config = mcfp::config::instance();
		fs::path toolsDir = config.get<std::string>("pdb-redo-tools-dir");
		std::ifstream f(toolsDir / "pdb_redo_stats.csv");
		if (not f.is_open())
			throw std::runtime_error("Could not open statistics file");

		std::string line;
		getline(f, line); // skip first

		while (getline(f, line))
		{
			std::vector<std::string> fld;
			zeep::split(fld, line, ",");
			if (fld.size() != 7)
				continue;
			m_stats.emplace_back(stod(fld[0]), stod(fld[1]), stod(fld[2]), stod(fld[3]), stod(fld[4]), stod(fld[5]), stod(fld[6]));
		}
	}
	
	std::vector<Stats> get_statistics_for_box_plot(double ureso)
	{
		std::vector<Stats> result;

		Stats test{};
		test.URESO = ureso;
		auto i1 = std::lower_bound(m_stats.begin(), m_stats.end(), test);
		auto i2 = std::upper_bound(m_stats.begin(), m_stats.end(), test);

		if (i2 - i1 >= 1000)
		{
			result.reserve(i2 - i1);
			std::copy(i1, i2, std::back_inserter(result));
		}
		else
		{
			result.reserve(1000);
			std::copy(i1, i2, std::back_inserter(result));

			while (result.size() < 1000 and (i1 != m_stats.begin() or i2 != m_stats.end()))
			{
				if (i1 == m_stats.begin())
				{
					result.emplace_back(*i2++);
					continue;
				}

				if (i2 == m_stats.end())
				{
					result.emplace_back(*--i1);
					continue;
				}

				auto d1 = ureso - i1->URESO;
				auto d2 = i2->URESO - ureso;

				if (d1 < d2)
					result.emplace_back(*--i1);
				else
					result.emplace_back(*i2++);
			}

			std::sort(result.begin(), result.end());
		}

		return result;
	}

	std::vector<Stats> m_stats;
};

// --------------------------------------------------------------------

class JobController : public zeep::http::html_controller
{
  public:
	JobController()
		: zeep::http::html_controller("job")
	{
		map_get("", &JobController::getJobListing);
		map_post("", &JobController::postJob, "mtz", "coords", "restraints", "sequence", "paired-refinement");

		map_get("output/{job-id}/{file}", &JobController::getOutputFile, "job-id", "file");
		map_get("image/{job-id}", &JobController::getImageFile, "job-id");
		map_get("result/{job-id}", &JobController::getResult, "job-id");
		map_get("entry/{job-id}", &JobController::getEntry, "job-id");
		map_delete("{job-id}", &JobController::deleteJob, "job-id");

		map_get("status", &JobController::getStatus, "ids");
	}

	zeep::http::reply getJobListing(const zeep::http::scope &scope)
	{
		auto credentials = scope.get_credentials();

		zeep::http::scope sub(scope);

		sub.put("page", "job");

		std::error_code ec;
		json runs;

		for (auto &run : RunService::instance().getRunsForUser(credentials["username"].get<std::string>()))
			runs.emplace_back(zeep::el::to_object(run));
		sub.put("runs", std::move(runs));

		return get_template_processor().create_reply_from_template("jobs", sub);
	}

	zeep::http::reply postJob(const zeep::http::scope &scope, const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
		const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, bool pairedRefinement)
	{
		auto credentials = scope.get_credentials();

		zeep::el::object params;
		params["paired"] = pairedRefinement;
		params["api"] = false;

		auto r = RunService::instance().submit(credentials["username"].get<std::string>(), coordinates, diffractionData, restraints, sequence, params);

		return zeep::http::reply::redirect("job", zeep::http::status_type::see_other);
	}

	zeep::http::reply getOutputFile(const zeep::http::scope &scope, uint64_t job_id, const std::string &file)
	{
		auto credentials = scope.get_credentials();
		auto run = RunService::instance().getRun(credentials["username"].get<std::string>(), job_id);

		zeep::http::reply result(zeep::http::status_type::ok);

		if (file == "zipped")
		{
			auto [f, name] = run.getZippedResultFile();
			result.set_content(std::move(f), "application/zip");
			result.set_header("content-disposition", "attachment; filename = \"" + name + "\"");
		}
		else
		{
			auto f = run.getResultFile(file);

			std::error_code ec;
			if (not fs::exists(f, ec))
				return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);

			result.set_content(std::make_unique<std::ifstream>(f), "application/octet-stream");
			result.set_header("content-disposition", "attachment; filename = \"" + f.filename().string() + "\"");
		}

		return result;
	}

	zeep::http::reply getImageFile(const zeep::http::scope &scope, uint64_t job_id)
	{
		auto credentials = scope.get_credentials();

		auto f = RunService::instance().getRun(credentials["username"].get<std::string>(), job_id).getImageFile();

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);

		zeep::http::reply result(zeep::http::status_type::ok);
		result.set_content(std::make_unique<std::ifstream>(f, std::ios::in | std::ios::binary), "image/png");
		return result;
	}

	zeep::http::reply getResult(const zeep::http::scope &scope, uint64_t job_id)
	{
		auto credentials = scope.get_credentials();

		auto r = RunService::instance().getRun(credentials["username"].get<std::string>(), job_id);

		zeep::http::scope sub(scope);

		sub.put("job-id", job_id);

		if (r.status == RunStatus::ENDED)
		{
			auto entry = create_entry_data(r, "/job/output/" + std::to_string(job_id));

			zeep::http::scope sub(scope);
			sub.put("entry", entry);

			return get_template_processor().create_reply_from_template("job-result", sub);
		}

		auto f = r.getResultFile("process.log");
		std::ifstream in(f);
		std::stringstream content;
		content << in.rdbuf();

		sub.put("log", content.str());
		sub.put("status", zeep::el::to_object(r.status));

		return get_template_processor().create_reply_from_template("job-error", sub);
	}

	zeep::http::reply getEntry(const zeep::http::scope &scope, uint64_t job_id)
	{
		auto credentials = scope.get_credentials();
		auto r = RunService::instance().getRun(credentials["username"].get<std::string>(), job_id);

		auto entry = create_entry_data(r, "/job/output/" + std::to_string(job_id));

		zeep::http::scope sub(scope);
		sub.put("entry", entry);

		return get_template_processor().create_reply_from_template("entry::tables", sub);
	}

	zeep::http::reply deleteJob(const zeep::http::scope &scope, uint64_t job_id)
	{
		auto credentials = scope.get_credentials();
		RunService::instance().deleteRun(credentials["username"].get<std::string>(), job_id);

		return zeep::http::reply::stock_reply(zeep::http::status_type::ok);
	}

	zeep::http::reply getStatus(const zeep::http::scope &scope, const std::vector<uint64_t> &job_ids)
	{
		auto credentials = scope.get_credentials();
		auto username = credentials["username"].get<std::string>();

		zeep::el::object status;
		for (auto job_id : job_ids)
		{
			auto r = RunService::instance().getRun(username, job_id);
			status.emplace_back(r.status);
		}

		zeep::http::reply reply(zeep::http::status_type::ok);
		reply.set_content(status);
		return reply;
	}
};

// --------------------------------------------------------------------

class RootController : public zeep::http::html_controller
{
  public:
	explicit RootController(const fs::path &pdb_db_dir)
		: m_db_dir(pdb_db_dir)
	{
		map_get_simple("", "index");
		map_get_simple("about", "about");
		map_get_simple("privacy-policy", "gdpr");
		map_get_simple("download", "download");
		map_get_simple("license", "license");
		map_get_simple("api-doc", "api-doc");

		map_get("client-api/**", &RootController::handle_client_api_file);

		map_get_file("{css,scripts,fonts,images}/");

		map_get("{others,schema}/**", &RootController::handle_others);

		map_post("entry", &RootController::handle_entry, "data.json", "link-url");

		map_get("nextUpdateRequest", &RootController::nextUpdateRequest);
	}

	// zeep::http::reply handle_entry(const zeep::http::scope &scope, const std::string &tokenID, const std::string &tokenSecret, const std::string &jobID);
	zeep::http::reply handle_entry(const zeep::http::scope &scope, const zeep::el::object &data, const std::optional<std::string> &link_url);

	// For the 'others' directory
	zeep::http::reply handle_others(const zeep::http::scope &scope)
	{
		return m_db_dir.create_reply_for_get_file(scope);
	}

	zeep::http::reply handle_client_api_file(const zeep::http::scope &scope);

	zeep::http::reply nextUpdateRequest(const zeep::http::scope &scope);

  private:
	zeep::http::file_based_html_template_processor m_db_dir;
};

zeep::http::reply RootController::handle_entry(const zeep::http::scope &scope, const zeep::el::object &data, const std::optional<std::string> &data_link)
{
	auto pdbID = data["pdbid"].get<std::string>();

	zeep::el::object entry{
		{ "id", data["pdbid"] },
		{ "dbEntry", false }
	};

	entry["data"] = data["properties"];
	entry["rama-angles"] = data["rama-angles"];

	if (data_link.has_value())
	{
		auto &link = entry["link"];
		std::string db = *data_link + "/";

		link["final_pdb"] = db + (pdbID + "_final.pdb");
		link["final_cif"] = db + (pdbID + "_final.cif");
		link["final_mtz"] = db + (pdbID + "_final.mtz");
		link["besttls_pdb"] = db + (pdbID + "_besttls.pdb");
		link["besttls_cif"] = db + (pdbID + "_besttls.cif");
		link["besttls_mtz"] = db + (pdbID + "_besttls.mtz");
		link["refmac_settings"] = db + (pdbID + ".refmac");
		link["homology_rest"] = db + "homology.rest";
		link["hbond_rest"] = db + "hbond.rest";
		link["metal_rest"] = db + "metal.rest";
		link["nucleic_rest"] = db + "nucleic.rest";
		link["wo"] = db + "wo/pdbout.txt";
		link["wf"] = db + "wf/pdbout.txt";

		// link["alldata"] = db + "zipped";
	}

	zeep::http::scope sub(scope);
	sub.put("entry", entry);

	return get_template_processor().create_reply_from_template("entry::tables", sub);
}

zeep::http::reply RootController::handle_client_api_file(const zeep::http::scope &scope)
{
	fs::path file = fs::path(scope["baseuri"].get<std::string>()).lexically_relative("client-api");

	mrsrc::rsrc data(file.string());

	if (not data)
		throw std::system_error(zeep::http::status_type::not_found);

	auto reply = zeep::http::reply::stock_reply(zeep::http::status_type::ok);
	reply.set_content(std::make_unique<mrsrc::istream>(data), "text/plain");
	return reply;
}

zeep::http::reply RootController::nextUpdateRequest(const zeep::http::scope
		& /*scope*/)
{
	std::ostringstream os;

	for (const auto &ur : DataService::instance().getAllUpdateRequests())
		os << ur.pdb_id << ',' << ur.user << '\n';

	zeep::http::reply result(zeep::http::status_type::ok);
	result.set_content(os.str(), "text/plain");
	return result;
}

// --------------------------------------------------------------------

class AdminController : public zeep::http::html_controller
{
  public:
	AdminController()
		: zeep::http::html_controller("admin")
	{
		map_get("", &AdminController::admin, "tab");
		map_get("job/{user}/{id}/output/{file}", &AdminController::handle_get_job_file, "user", "id", "file");
		map_get("job/{user}/{id}", &AdminController::job, "user", "id");
		map_delete_request("job/{user}/{id}", &AdminController::handle_delete_job, "user", "id");
		map_delete_request("user/{id}", &AdminController::handle_delete_user, "id");
		map_delete_request("token/{id}", &AdminController::handle_delete_token, "id");
		map_delete_request("update/{id}", &AdminController::handle_delete_update, "id");
	}

	zeep::http::reply admin(const zeep::http::scope &scope, const std::optional<std::string> &tab);
	zeep::http::reply job(const zeep::http::scope &scope, const std::string &user, uint64_t id);
	zeep::http::reply handle_get_job_file(const zeep::http::scope &scope, const std::string &user, uint64_t id, const std::string &file);

	void handle_delete_job(const std::string &user, uint64_t id);
	void handle_delete_user(const zeep::http::scope &scope, uint64_t id);
	void handle_delete_token(uint64_t id);
	void handle_delete_update(uint64_t id);
};

zeep::http::reply AdminController::admin(const zeep::http::scope &scope, const std::optional<std::string> &tab)
{
	zeep::http::scope sub(scope);

	sub.put("page", "admin");

	std::string active = tab.value_or("users");
	sub.put("tab", active);

	if (active == "tokens")
		sub.put("tokens", zeep::el::to_object(TokenService::instance().getAllTokens()));
	else if (active == "users")
		sub.put("users", zeep::el::to_object(UserService::instance().getAllUsers()));
	else if (active == "jobs")
		sub.put("runs", zeep::el::to_object(RunService::instance().getAllRuns()));
	else if (active == "updates")
		sub.put("updates", zeep::el::to_object(DataService::instance().getAllUpdateRequests()));

	return get_template_processor().create_reply_from_template("admin", sub);
}

zeep::http::reply AdminController::job(const zeep::http::scope &scope, const std::string &user, uint64_t job_id)
{
	auto run = RunService::instance().getRun(user, job_id);

	if (run.status == RunStatus::ENDED)
	{
		auto entry = create_entry_data(run, "/admin/job/" + user + '/' + std::to_string(job_id) + "/output/");

		zeep::http::scope sub(scope);
		sub.put("entry", entry);

		return get_template_processor().create_reply_from_template("admin-job-result", sub);
	}

	auto f = run.getResultFile("process.log");

	std::error_code ec;
	if (fs::exists(f, ec))
	{
		zeep::http::reply result(zeep::http::status_type::ok);
		result.set_content(std::make_unique<std::ifstream>(f), "text/plain");
		return result;
	}

	return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);
}

zeep::http::reply AdminController::handle_get_job_file(const zeep::http::scope
		& /*scope*/, const std::string &user, uint64_t job_id, const std::string &file)
{
	auto run = RunService::instance().getRun(user, job_id);

	zeep::http::reply result(zeep::http::status_type::ok);

	if (file == "zipped")
	{
		auto [f, name] = run.getZippedResultFile();
		result.set_content(std::move(f), "application/zip");
		result.set_header("content-disposition", "attachment; filename = \"" + name + "\"");
	}
	else
	{
		auto f = run.getResultFile(file);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);

		result.set_content(std::make_unique<std::ifstream>(f), "application/octet-stream");
		result.set_header("content-disposition", "attachment; filename = \"" + f.filename().string() + "\"");
	}

	return result;
}

void AdminController::handle_delete_job(const std::string &user, uint64_t id)
{
	RunService::instance().deleteRun(user, id);
}

void AdminController::handle_delete_user(const zeep::http::scope &scope, uint64_t id)
{
	auto &user_service = UserService::instance();
	auto me = user_service.getUser(scope.get_credentials()["username"].get<std::string>());
	if (me.id == id)
		throw std::runtime_error("Are you serious, do you want to throw away yourself?");

	user_service.deleteUser(id);
}

void AdminController::handle_delete_token(uint64_t id)
{
	TokenService::instance().deleteToken(id);
}

void AdminController::handle_delete_update(uint64_t id)
{
	DataService::instance().deleteUpdateRequest(id);
}

// --------------------------------------------------------------------

class DbController : public zeep::http::html_controller
{
  public:
	DbController()
		: zeep::http::html_controller("db")
	{
		map_post("get", &DbController::handle_get, "pdb-id");

		map_get("entry", &DbController::handle_entry, "pdb-id", "attic");
		map_post("entry", &DbController::handle_entry, "pdb-id", "attic");

		map_get("update/{id}", &DbController::handle_update, "id");

		map_get("{id}/zipped", &DbController::handle_zipped, "id");
		map_get("{id}/{file}", &DbController::handle_pdb_file, "id", "file");

		// since the uri class was added to libzeep:
		map_get("{id}/wo/{file}", &DbController::handle_file_wo, "id", "file");
		map_get("{id}/wf/{file}", &DbController::handle_file_wf, "id", "file");
		map_get("{id}/wc/{file}", &DbController::handle_file_wc, "id", "file");

		map_get("{id}/attic/{attic}/zipped", &DbController::handle_zipped_attic, "id", "attic");
		map_get("{id}/attic/{attic}/{file}", &DbController::handle_file_attic, "id", "file", "attic");

		map_get("{id}", &DbController::handle_show, "id");
	}

	zeep::http::reply handle_get(const zeep::http::scope &scope, std::string pdbID);
	zeep::http::reply handle_entry(const zeep::http::scope &scope, std::string pdbID, const std::optional<std::string> &attic);
	zeep::http::reply handle_show(const zeep::http::scope &scope, std::string pdbID);

	zeep::http::reply handle_update(const zeep::http::scope &scope, std::string pdbID)
	{
		zeep::to_lower(pdbID);

		try
		{
			auto credentials = scope.get_credentials();
			if (not credentials)
				throw std::runtime_error("You cannot request an update for this PDB-REDO entry since you are not logged in");

			User user = UserService::instance().getUser(credentials["username"].get<std::string>());
			DataService::instance().requestUpdate(pdbID, user);

			return zeep::http::reply::redirect("/db/" + pdbID);
		}
		catch (...)
		{
			std::throw_with_nested(std::runtime_error("The request for updating failed, did you already request an update before for this entry?"));
		}
	}

	zeep::http::reply handle_zipped(const zeep::http::scope
			& /*scope*/, std::string pdbID)
	{
		zeep::to_lower(pdbID);

		auto &&[is, name] = DataService::instance().getZipFile(pdbID);

		zeep::http::reply rep{ zeep::http::status_type::ok };
		rep.set_content(std::move(is), "application/zip");
		rep.set_header("content-disposition", "attachment; filename = \"" + name + '"');

		return rep;
	}

	zeep::http::reply handle_file_wo(const zeep::http::scope &scope, std::string pdbID, const std::string &file)
	{
		return handle_pdb_file(scope, std::move(pdbID), fs::path("wo") / file);
	}

	zeep::http::reply handle_file_wf(const zeep::http::scope &scope, std::string pdbID, const std::string &file)
	{
		return handle_pdb_file(scope, std::move(pdbID), fs::path("wf") / file);
	}

	zeep::http::reply handle_file_wc(const zeep::http::scope &scope, std::string pdbID, const std::string &file)
	{
		return handle_pdb_file(scope, std::move(pdbID), fs::path("wc") / file);
	}

	zeep::http::reply handle_pdb_file(const zeep::http::scope
			& /*scope*/, std::string pdbID, std::string file)
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
			return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);

		zeep::http::reply result(zeep::http::status_type::ok);
		result.set_content(std::make_unique<std::ifstream>(f), "application/octet-stream");
		result.set_header("content-disposition", "attachment; filename = \"" + f.filename().string() + "\"");
		return result;
	}

	zeep::http::reply handle_zipped_attic(const zeep::http::scope
			& /*scope*/, std::string pdbID, const std::string &attic)
	{
		zeep::to_lower(pdbID);

		auto &&[is, name] = DataService::instance().getZipFile(pdbID, attic);

		zeep::http::reply rep{ zeep::http::status_type::ok };
		rep.set_content(std::move(is), "application/zip");
		rep.set_header("content-disposition", "attachment; filename = \"" + name + '"');

		return rep;
	}

	zeep::http::reply handle_file_attic(const zeep::http::scope
			& /*scope*/, std::string pdbID, const std::string &file, const std::string &attic)
	{
		zeep::to_lower(pdbID);

		auto f = DataService::instance().getFile(pdbID, file, attic);

		std::error_code ec;
		if (not fs::exists(f, ec))
			return zeep::http::reply::stock_reply(zeep::http::status_type::not_found);

		zeep::http::reply result(zeep::http::status_type::ok);
		result.set_content(std::make_unique<std::ifstream>(f), "application/octet-stream");
		result.set_header("content-disposition", "attachment; filename = \"" + f.filename().string() + "\"");
		return result;
	}
};

zeep::http::reply DbController::handle_get(const zeep::http::scope
		& /*scope*/, std::string pdbID)
{
	DataService::validatePDBID(pdbID);

	zeep::to_lower(pdbID);
	return zeep::http::reply::redirect(pdbID, zeep::http::status_type::see_other);
}

zeep::http::reply DbController::handle_show(const zeep::http::scope &scope, std::string pdbID)
{
	auto &ds = DataService::instance();

	zeep::to_lower(pdbID);

	zeep::http::scope sub(scope);

	auto pdbRedoVersion = ds.version();

	sub.put("pdb-id", pdbID);
	sub.put("version", pdbRedoVersion);

	try
	{
		auto data = ds.getData(pdbID);
		if (data)
		{
			auto entry = create_entry_data(data, "/db/" + pdbID, ds.getFileList(pdbID));

			entry["id"] = pdbID;
			entry["dbEntry"] = true;
			entry["status"] = zeep::el::to_object(ds.getUpdateStatus(pdbID));

			sub.put("entry", entry);

			return get_template_processor().create_reply_from_template("db-entry", sub);
		}
	}
	catch (...)
	{
	}

	auto attic = ds.getLatestAttic(pdbID);
	if (not attic.empty())
	{
		try
		{
			auto data = ds.getData(pdbID, attic);
			auto entry = create_entry_data(data, "/db/" + pdbID + "/attic/" + attic + '/', ds.getFileList(pdbID, attic));

			entry["id"] = pdbID;
			entry["dbEntry"] = true;
			entry["status"] = zeep::el::to_object(ds.getUpdateStatus(pdbID));

			sub.put("entry", entry);
			sub.put("attic", attic);

			return get_template_processor().create_reply_from_template("db-entry", sub);
		}
		catch (...)
		{
		}
	}

	// OK, that failed. Find out whynot

	auto whynot = ds.getWhyNot(pdbID);
	sub.put("whynot", whynot);

	return get_template_processor().create_reply_from_template("why-not", sub);
}

zeep::http::reply DbController::handle_entry(const zeep::http::scope &scope, std::string pdbID, const std::optional<std::string> &attic)
{
	zeep::to_lower(pdbID);

	auto dataJsonFile = DataService::instance().getFile(pdbID, "data.json", attic);
	std::ifstream dataJson(dataJsonFile);

	if (not dataJson.is_open())
		throw std::system_error(zeep::http::status_type::not_found);

	zeep::el::object data = zeep::el::object::parse_JSON(dataJson);

	auto entry = create_entry_data(data, "/db/" + pdbID, DataService::instance().getFileList(pdbID));

	zeep::http::scope sub(scope);
	sub.put("entry", entry);

	return get_template_processor().create_reply_from_template("entry::tables", sub);
}

// --------------------------------------------------------------------

class pdb_entry_error_handler : public zeep::http::error_handler
{
  public:
	bool create_error_reply(const zeep::http::request &req, const std::exception_ptr &eptr, zeep::http::reply &reply) override
	{
		bool result = false;

		try
		{
			std::rethrow_exception(eptr);
		}
		catch (const zeep::http::status_type &err)
		{
			if (err == zeep::http::status_type::unprocessable_entity)
			{
				auto pdb_id = req.get_parameter("pdb-id");
				zeep::http::scope scope(m_server, req);
				if (pdb_id.has_value())
					scope.put("pdb-id", *pdb_id);
				reply = m_server->get_template_processor().create_reply_from_template("entry-not-found", scope);
				reply.set_status(zeep::http::status_type::unprocessable_entity);
				result = true;
			}
		}
		catch (...)
		{
		}

		return result;
	}
};

// --------------------------------------------------------------------

// recursively print exception whats:
void print_what(const std::exception &e)
{
	std::cerr << e.what() << '\n';
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
		using namespace std::literals;

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
			mcfp::make_option<std::string>("allow-origin", "*", "The value for the CORS header Access-Control-Allow-Origin"),
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
			mcfp::make_option<std::string>("final-file-pattern", "${id}_final.cif", "Pattern for the final xyzin file"));

		std::error_code ec;
		config.parse(argc, argv, ec);
		if (ec)
			throw std::runtime_error("Error parsing arguments: " + ec.message());

		if (config.has("version"))
		{
			write_version_string(std::cout, config.has("verbose"));
			return 0;
		}

		if (config.has("help"))
		{
			std::cerr << config << '\n';
			return config.has("help") ? 0 : 1;
		}

		config.parse_config_file("config", "prsmd.conf", { fs::current_path().string(), "/etc/" }, ec);
		if (ec)
			throw std::runtime_error("Error parsing config file: " + ec.message());

		// --------------------------------------------------------------------

		if (config.has("help") or config.operands().empty())
		{
			std::cerr << config << '\n'
					  << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options

  )";
			return config.has("help") ? 0 : 1;
		}

		for (const char *option : { "pdb-redo-services-dir", "pdb-redo-db-dir", "pdb-redo-tools-dir" })
		{
			if (config.has(option))
				continue;
			std::cerr << "Missing " << option << " option\n";
			return 1;
		}

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
			std::cerr << "starting with created secret " << secret << '\n';
		}

		std::string context;
		if (config.has("context"))
			context = config.get<std::string>("context");

		std::string allow_origin = config.get("allow-origin");

		zeep::http::daemon server([secret, context, allow_origin, &config]()
			{
			auto sc = new zeep::http::security_context(secret, UserService::instance());
			sc->add_rule("/admin", { "ADMIN" });
			sc->add_rule("/admin/**", { "ADMIN" });
			sc->add_rule("/{job,tokens}", { "USER" });
			sc->add_rule("/{job,others}/**", { "USER" });

			sc->add_rule("/{change-password,update-info,token,delete,ccp4-token-request}", { "USER" });

			sc->add_rule("/**", {});

			sc->register_password_encoder<PasswordEncoder>();
			sc->set_validate_csrf(true);
			sc->set_jwt_exp(std::chrono::days{1});

			auto s = new zeep::http::server(sc);

			auto access_control = new zeep::http::access_control(allow_origin, true);
			access_control->add_allowed_header("X-PDB-REDO-Date");
			access_control->add_allowed_header("Authorization");
			s->set_access_control(access_control);
	
			if (not context.empty())
				s->set_context_path(context);

			s->add_error_handler(new prsm_db_error_handler());
			s->add_error_handler(new pdb_entry_error_handler());

#ifndef NDEBUG
			s->set_template_processor(new zeep::http::file_based_html_template_processor("docroot"));
#else
			s->set_template_processor(new zeep::http::rsrc_based_html_template_processor());
#endif

			s->add_controller(new RootController(config.get("pdb-redo-db-dir")));
			s->add_controller(new UserHTMLController());
			s->add_controller(new AdminController());
			s->add_controller(new DbController());
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
				std::cout << "starting server at http://[" << address << "]:" << port << '/' << '\n';
			else
				std::cout << "starting server at http://" << address << ':' << port << '/' << '\n';

			if (config.has("no-daemon"))
				result = server.run_foreground(address, port);
			else
				result = server.start(address, port, 8, user);
		}
		else if (command == "stop")
			result = server.stop();
		else if (command == "status")
			result = server.status();
		else if (command == "reload")
			result = server.reload();
		else
		{
			std::cerr << "Invalid command\n";
			result = 1;
		}
	}
	catch (const std::exception &ex)
	{
		print_what(ex);
		return 1;
	}

	return result;
}
