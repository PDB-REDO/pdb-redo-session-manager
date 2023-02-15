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

#include "user-service.hpp"

#include <zeep/json/element.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct UpdateRequest
{
	unsigned long id;
	std::string user;
	std::string pdb_id;
	std::chrono::time_point<std::chrono::system_clock> created;
	double version;

	UpdateRequest(const pqxx::row &row);
	// UpdateRequest &operator=(const pqxx::row &row);

	template <typename Archive>
	void serialize(Archive &ar, unsigned long v)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("pdb_id", pdb_id)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("version", version);
	}	
};

struct UpdateStatus
{
	bool ok;
	std::optional<float> pendingVersion;

	explicit operator bool() { return ok; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("ok", ok)
		   & zeep::make_nvp("version", pendingVersion);
	}
};

class DataService
{
  public:
	static DataService &instance();

	std::vector<std::string> getFileList(const std::string &pdbID);
	std::filesystem::path getFile(const std::string &pdbID, const std::string &file);

	std::tuple<std::istream *, std::string> getZipFile(const std::string &pdbID);
	zeep::json::element getData(const std::string &pdbID);

	UpdateStatus getUpdateStatus(const std::string &pdbID);
	void requestUpdate(const std::string &pdbID, const User &user);
	void deleteUpdateRequest(int id);

	float version() const;

	std::vector<UpdateRequest> get_all_update_requests();

	std::string getWhyNot(const std::string &pdbID);

  private:
	DataService();

	DataService(const DataService &) = delete;
	DataService &operator=(const DataService &) = delete;

	void checkUpdateRequests();

	std::filesystem::path m_data_dir;
	std::mutex m_mutex;
};
