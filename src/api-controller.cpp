/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2023 NKI/AVL, Netherlands Cancer Institute
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

#include <algorithm>
#include <zeep/crypto.hpp>
#include <zeep/http/status.hpp>
#include <zeep/http/security.hpp>
#include <zeep/http/status.hpp>
#include <zeep/uri.hpp>

namespace fs = std::filesystem;

using json = zeep::el::object;

// --------------------------------------------------------------------

JobInfo::JobInfo(const Run &run)
	: id(run.id)
	, status(run.status)
	, date(run.date)
	, started(run.started)
	, score(run.score)
	, input(run.input)
{
}

// --------------------------------------------------------------------

uint64_t thread_local APIRESTController_v2::s_token_id = 0;


APIRESTController_v2::APIRESTController_v2()
	: zeep::http::controller("api")
{
	// return a list of runs
	map_get_request("run", &APIRESTController_v2::getAllRuns);

	// Submit a run (job)
	map_post_request("run", &APIRESTController_v2::createJob,
		"mtz-file", "pdb-file", "restraints-file", "sequence-file", "parameters");

	// return info for a run
	map_get_request("run/{run}", &APIRESTController_v2::getRun, "run");

	// get a list of the files in output
	map_get_request("run/{run}/output", &APIRESTController_v2::getResultFileList, "run");

	// get all results file zipped into an archive
	map_get_request("run/{run}/output/zipped", &APIRESTController_v2::getZippedResultFile, "run");

	// get a result file
	map_get_request("run/{run}/output/{file}", &APIRESTController_v2::getResultFile, "run", "file");

	// delete a run
	map_delete_request("run/{run}", &APIRESTController_v2::deleteRun, "run");
}

bool APIRESTController_v2::handle_request(zeep::http::request &req, zeep::http::reply &rep)
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

			if (!authorization.starts_with("PDB-REDO-api "))
				throw zeep::http::unauthorized_exception();

			std::vector<std::string> signedHeaders;

			std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

			std::smatch m;
			if (not std::regex_search(authorization, m, re))
				throw zeep::http::unauthorized_exception();

			std::vector<std::string> credentials;
			std::string credential = m[1].str();

			for (std::string::size_type i = 0, j = credential.find('/');;)
			{
				credentials.push_back(credential.substr(i, j - i));
				if (j == std::string::npos)
					break;
				i = j + 1;
				j = credential.find('/', i);
			}

			if (credentials.size() != 3 or credentials[2] != "pdb-redo-api")
				throw zeep::http::unauthorized_exception();

			auto signature = zeep::decode_base64(m[3].str());

			// Validate the signature

			// canonical request

			std::vector<std::tuple<std::string, std::string>> params;
			for (auto &p : req.get_parameters())
				params.emplace_back(p.first, p.second);
			std::ranges::sort(params);
			std::ostringstream ps;
			auto n = params.size();
			for (auto &[name, value] : params)
			{
				ps << zeep::encode_url(name);
				if (not value.empty())
					ps << '=' << zeep::encode_url(value);
				if (n-- > 1)
					ps << '&';
			}

			auto contentHash = zeep::encode_base64(zeep::sha256(req.get_payload()));

			auto pathPart = zeep::uri(req.get_uri().get_path().string(), m_server->get_context_path());

			std::string host = req.get_header("X-Forwarded-Host");
			if (host.empty())
				host = req.get_header("host");			

			std::ostringstream ss;
			ss << req.get_method() << '\n'
				<< pathPart.get_path() << '\n'
				<< ps.str() << '\n'
				<< host << '\n'
				<< contentHash;

			auto canonicalRequest = ss.str();
			auto canonicalRequestHash = zeep::encode_base64(zeep::sha256(canonicalRequest));

			// string to sign
			auto timestamp = req.get_header("X-PDB-REDO-Date");

			std::ostringstream ss2;
			ss2 << "PDB-REDO-api\n"
				<< timestamp << '\n'
				<< credential << '\n'
				<< canonicalRequestHash;
			auto stringToSign = ss2.str();

			auto tokenid = credentials[0];
			auto date = credentials[1];

			auto secret = TokenService::instance().getTokenByID(std::stoul(tokenid)).secret;
			auto keyString = "PDB-REDO" + secret;

			auto key = zeep::hmac_sha256(date, keyString);
			if (zeep::hmac_sha256(stringToSign, key) != signature)
				throw zeep::http::unauthorized_exception();
			
			s_token_id = stoi(credentials[0]);

			result = zeep::http::controller::handle_request(req, rep);
		}
		catch (const std::exception &e)
		{
			using namespace std::literals;

			rep.set_content(json({ { "error", "invalid credentials" } }));
			rep.set_status(zeep::http::status_type::unauthorized);

			result = true;
		}
	}

	// reset, just in case
	s_token_id = 0;

	return result;
}

// CRUD routines

// Token APIRESTController_v2::getToken()
// {
// 	return getTokenForRequest();
// }

// void APIRESTController_v2::deleteToken()
// {
// 	TokenService::instance().deleteToken(s_token_id);
// }

std::vector<JobInfo> APIRESTController_v2::getAllRuns()
{
	auto token = getTokenForRequest();

	std::vector<JobInfo> result;
	for (auto &run : RunService::instance().getRunsForUser(token.user))
		result.emplace_back(run);

	return result;
}

JobInfo APIRESTController_v2::createJob(const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
	const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, json params)
{
	auto token = getTokenForRequest();

	params["api"] = true;

	return RunService::instance().submit(token.user, coordinates, diffractionData, restraints, sequence, params);
}

JobInfo APIRESTController_v2::getRun(uint64_t runID)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID);
}

std::vector<std::string> APIRESTController_v2::getResultFileList(uint64_t runID)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID).getResultFileList();
}

fs::path APIRESTController_v2::getResultFile(uint64_t runID, const std::string &file)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID).getResultFile(file);
}

zeep::http::reply APIRESTController_v2::getZippedResultFile(uint64_t runID)
{
	auto token = getTokenForRequest();

	auto &&[is, name] = RunService::instance().getRun(token.user, runID).getZippedResultFile();

	zeep::http::reply rep{ zeep::http::status_type::ok };
	rep.set_content(std::move(is), "application/zip");
	rep.set_header("content-disposition", "attachment; filename = \"" + name + '"');

	return rep;
}

void APIRESTController_v2::deleteRun(uint64_t runID)
{
	auto token = getTokenForRequest();

	return RunService::instance().deleteRun(token.user, runID);
}

// --------------------------------------------------------------------

APIRESTController_v1::APIRESTController_v1()
	: APIRESTController_v2()
{
	// get session info
	map_get_request("session/{id}", &APIRESTController_v1::getToken, "id");

	// delete a session
	map_delete_request("session/{id}", &APIRESTController_v1::deleteToken, "id");

	// return a list of runs
	map_get_request("session/{id}/run", &APIRESTController_v1::getAllRuns, "id");

	// Submit a run (job)
	map_post_request("session/{id}/run", &APIRESTController_v1::createJob, "id",
		"mtz-file", "pdb-file", "restraints-file", "sequence-file", "parameters");

	// return info for a run
	map_get_request("session/{id}/run/{run}", &APIRESTController_v1::getRun, "id", "run");

	// get a list of the files in output
	map_get_request("session/{id}/run/{run}/output", &APIRESTController_v1::getResultFileList, "id", "run");

	// get all results file zipped into an archive
	map_get_request("session/{id}/run/{run}/output/zipped", &APIRESTController_v1::getZippedResultFile, "id", "run");

	// get a result file
	map_get_request("session/{id}/run/{run}/output/{file}", &APIRESTController_v1::getResultFile, "id", "run", "file");

	// delete a run
	map_delete_request("session/{id}/run/{run}", &APIRESTController_v1::deleteRun, "id", "run");
}

void APIRESTController_v1::checkTokenID(uint64_t tokenID)
{
	if (tokenID != s_token_id)
		throw std::system_error(zeep::http::status_type::forbidden);
}

// CRUD routines

Token APIRESTController_v1::getToken(uint64_t id)
{
	checkTokenID(id);
	return getTokenForRequest();
}

void APIRESTController_v1::deleteToken(uint64_t id)
{
	checkTokenID(id);
	TokenService::instance().deleteToken(s_token_id);
}

std::vector<JobInfo> APIRESTController_v1::getAllRuns(uint64_t id)
{
	checkTokenID(id);
	return APIRESTController_v2::getAllRuns();
}

JobInfo APIRESTController_v1::createJob(uint64_t tokenID, const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
	const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, const json &params)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::createJob(diffractionData, coordinates, restraints, sequence, params);
}

JobInfo APIRESTController_v1::getRun(uint64_t tokenID, uint64_t runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getRun(runID);
}

std::vector<std::string> APIRESTController_v1::getResultFileList(uint64_t tokenID, uint64_t runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getResultFileList(runID);
}

fs::path APIRESTController_v1::getResultFile(uint64_t tokenID, uint64_t runID, const std::string &file)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getResultFile(runID, file);
}

zeep::http::reply APIRESTController_v1::getZippedResultFile(uint64_t tokenID, uint64_t runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getZippedResultFile(runID);
}

void APIRESTController_v1::deleteRun(uint64_t tokenID, uint64_t runID)
{
	checkTokenID(tokenID);
	APIRESTController_v2::deleteRun(runID);
}
