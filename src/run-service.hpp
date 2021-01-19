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

#pragma once

#include <string>
#include <memory>
#include <filesystem>

#include <zeep/json/element.hpp>
#include <zeep/http/request.hpp>

enum class RunStatus
{
	UNDEFINED,
    REGISTERED,
    STARTING,
    QUEUED,
    RUNNING,
    STOPPING,
    STOPPED,
    ENDED,
    DELETING
};

// --------------------------------------------------------------------

struct Run
{
	uint32_t id;
	std::string user;
	RunStatus status;

	static Run create(const std::filesystem::path& dir, const std::string& username);

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("status", status);
	}
};

class RunService
{
  public:

	static void init(const std::string& runsDir);
	static RunService& instance();

	RunService(const RunService&) = delete;
	RunService& operator=(const RunService&) = delete;

	Run submit(const std::string& user, const zeep::http::file_param& pdb, const zeep::http::file_param& mtz,
		const zeep::http::file_param& restraints, const zeep::http::file_param& sequence, const zeep::json::element& params);

	std::vector<Run> get_runs_for_user(const std::string& username);
	Run get_run(const std::string& username, unsigned long runID);

	std::filesystem::path get_result_file(const std::string& username, unsigned long runID, const std::string& file);

  private:

	RunService(const std::string& runsDir);

	static std::unique_ptr<RunService> sInstance;
	std::filesystem::path m_runsdir;
};