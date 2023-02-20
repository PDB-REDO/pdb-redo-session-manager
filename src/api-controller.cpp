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

#include <zeep/crypto.hpp>
#include <zeep/http/security.hpp>
#include <zeep/http/uri.hpp>

namespace zh = zeep::http;
namespace fs = std::filesystem;

using json = zeep::json::element;

// --------------------------------------------------------------------

unsigned long thread_local APIRESTController_v2::s_token_id = 0;


APIRESTController_v2::APIRESTController_v2()
	: zh::rest_controller("api")
{
	// get token info
	map_get_request("", &APIRESTController_v2::getToken);

	// delete a token
	map_delete_request("", &APIRESTController_v2::deleteToken);

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

bool APIRESTController_v2::handle_request(zh::request &req, zh::reply &rep)
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

			auto secret = TokenService::instance().getTokenByID(std::stoul(tokenid)).secret;
			auto keyString = "PDB-REDO" + secret;

			auto key = zeep::hmac_sha256(date, keyString);
			if (zeep::hmac_sha256(stringToSign, key) != signature)
				throw zh::unauthorized_exception();
			
			s_token_id = stoi(credentials[0]);

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

	// reset, just in case
	s_token_id = 0;

	return result;
}

// CRUD routines

Token APIRESTController_v2::getToken()
{
	return getTokenForRequest();
}

void APIRESTController_v2::deleteToken()
{
	TokenService::instance().deleteToken(s_token_id);
}

std::vector<Run> APIRESTController_v2::getAllRuns()
{
	auto token = getTokenForRequest();

	return RunService::instance().getRunsForUser(token.user);
}

Run APIRESTController_v2::createJob(const zh::file_param &diffractionData, const zh::file_param &coordinates,
	const zh::file_param &restraints, const zh::file_param &sequence, const json &params)
{
	auto token = getTokenForRequest();

	return RunService::instance().submit(token.user, coordinates, diffractionData, restraints, sequence, params);
}

Run APIRESTController_v2::getRun(unsigned long runID)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID);
}

std::vector<std::string> APIRESTController_v2::getResultFileList(unsigned long runID)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID).getResultFileList();
}

fs::path APIRESTController_v2::getResultFile(unsigned long runID, const std::string &file)
{
	auto token = getTokenForRequest();

	return RunService::instance().getRun(token.user, runID).getResultFile(file);
}

zh::reply APIRESTController_v2::getZippedResultFile(unsigned long runID)
{
	auto token = getTokenForRequest();

	const auto &[is, name] = RunService::instance().getRun(token.user, runID).getZippedResultFile();

	zh::reply rep{ zh::ok };
	rep.set_content(is, "application/zip");
	rep.set_header("content-disposition", "attachement; filename = \"" + name + '"');

	return rep;
}

void APIRESTController_v2::deleteRun(unsigned long runID)
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

void APIRESTController_v1::checkTokenID(unsigned long tokenID)
{
	if (tokenID != s_token_id)
		throw zh::forbidden;
}

// CRUD routines

Token APIRESTController_v1::getToken(unsigned long id)
{
	checkTokenID(id);
	return APIRESTController_v2::getToken();
}

void APIRESTController_v1::deleteToken(unsigned long id)
{
	checkTokenID(id);
	APIRESTController_v2::deleteToken();
}

std::vector<Run> APIRESTController_v1::getAllRuns(unsigned long id)
{
	checkTokenID(id);
	return APIRESTController_v2::getAllRuns();
}

Run APIRESTController_v1::createJob(unsigned long tokenID, const zh::file_param &diffractionData, const zh::file_param &coordinates,
	const zh::file_param &restraints, const zh::file_param &sequence, const json &params)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::createJob(diffractionData, coordinates, restraints, sequence, params);
}

Run APIRESTController_v1::getRun(unsigned long tokenID, unsigned long runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getRun(runID);
}

std::vector<std::string> APIRESTController_v1::getResultFileList(unsigned long tokenID, unsigned long runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getResultFileList(runID);
}

fs::path APIRESTController_v1::getResultFile(unsigned long tokenID, unsigned long runID, const std::string &file)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getResultFile(runID, file);
}

zh::reply APIRESTController_v1::getZippedResultFile(unsigned long tokenID, unsigned long runID)
{
	checkTokenID(tokenID);
	return APIRESTController_v2::getZippedResultFile(runID);
}

void APIRESTController_v1::deleteRun(unsigned long tokenID, unsigned long runID)
{
	checkTokenID(tokenID);
	APIRESTController_v2::deleteRun(runID);
}
