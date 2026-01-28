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

#pragma once

#include "run-service.hpp"
#include "token-service.hpp"

#include <zeep/http/controller.hpp>

// --------------------------------------------------------------------

struct JobInfo
{
	uint32_t id;
	RunStatus status;
	std::chrono::time_point<std::chrono::system_clock> date;
	std::optional<std::chrono::time_point<std::chrono::system_clock>> started;
	std::optional<Score> score;
	std::vector<std::string> input;

	JobInfo(const Run &run); // NOLINT(hicpp-explicit-conversions)

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("id", id)
		   & zeem::name_value_pair("status", status)
		   & zeem::name_value_pair("date", date)
		   & zeem::name_value_pair("started-date", started)
		   & zeem::name_value_pair("score", score)
		   & zeem::name_value_pair("input", input);
		// clang-format on
	}
};

class APIRESTController_v2 : public zeep::http::controller
{
  public:
	APIRESTController_v2();

	// Bottle neck, to validate access tokens on requests
	bool handle_request(zeep::http::request &req, zeep::http::reply &rep) override;

	// CRUD routines
	std::vector<JobInfo> getAllRuns();

	JobInfo createJob(const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
		const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, zeep::el::object params);

	JobInfo getRun(uint64_t runID);

	std::vector<std::string> getResultFileList(uint64_t runID);

	std::filesystem::path getResultFile(uint64_t runID, const std::string &file);

	zeep::http::reply getZippedResultFile(uint64_t runID);

	void deleteRun(uint64_t runID);

  protected:
	[[nodiscard]] Token getTokenForRequest() const
	{
		return TokenService::instance().getTokenByID(s_token_id);
	}

	std::filesystem::path m_pdb_redo_dir;
	static thread_local uint64_t s_token_id;
};

class APIRESTController_v1 : public APIRESTController_v2
{
  public:
	APIRESTController_v1();

	Token getToken(uint64_t id);
	void deleteToken(uint64_t id);
	std::vector<JobInfo> getAllRuns(uint64_t id);

	JobInfo createJob(uint64_t tokenID, const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
		const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, const zeep::el::object &params);

	JobInfo getRun(uint64_t tokenID, uint64_t runID);

	std::vector<std::string> getResultFileList(uint64_t tokenID, uint64_t runID);

	std::filesystem::path getResultFile(uint64_t tokenID, uint64_t runID, const std::string &file);

	zeep::http::reply getZippedResultFile(uint64_t tokenID, uint64_t runID);

	void deleteRun(uint64_t tokenID, uint64_t runID);

	void checkTokenID(uint64_t tokenID);
};