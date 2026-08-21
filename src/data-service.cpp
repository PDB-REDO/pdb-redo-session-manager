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

#include "data-service.hpp"

#include "prsm-db-connection.hpp"
#include "zip-support.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <mcfp/mcfp.hpp>
#include <numeric>
#include <zeep/http/client.hpp>
#include <zeep/http/reply.hpp>

namespace fs = std::filesystem;

// --------------------------------------------------------------------

auto sanitizePath(const fs::path &dir, const fs::path &file) -> fs::path
{
	std::error_code ec;

	auto result = fs::weakly_canonical(dir / file, ec);
	auto s = result.generic_string();

	if (ec or not s.starts_with(dir.generic_string()))
		result.clear();

	return result;
}

void DataService::validatePDBID(std::string_view pdbID)
{
	auto test = pdbID.starts_with("pdb_") ? pdbID.substr(4) : pdbID;

	bool valid = test.length() == 4 or test.length() == 8;
	if (valid)
	{
		for (auto ch : test)
		{
			if (not std::isalnum(static_cast<uint8_t>(ch)))
			{
				valid = false;
				break;
			}
		}
	}

	if (not valid)
		throw InvalidPDBIDError(pdbID);
}

// --------------------------------------------------------------------

UpdateRequest::UpdateRequest(const pqxx::row &row)
{
	id = row.at("id").as<uint64_t>();
	user = row.at("user").as<std::string>();
	pdb_id = row.at("pdb_id").as<std::string>();
	created = parse_timestamp(row.at("created").as<std::string>());
	version = row.at("version").as<double>();
}

// --------------------------------------------------------------------

DataService &DataService::instance()
{
	static DataService s_instance;
	return s_instance;
}

DataService::DataService()
{
	auto &config = mcfp::config::instance();

	m_data_dir = config.get<std::string>("pdb-redo-db-dir");
	if (not fs::exists(m_data_dir))
		throw std::runtime_error("PDB-REDO data directory (" + m_data_dir.string() + ") does not exists");
}

UpdateStatus DataService::getUpdateStatus(const std::string &pdbID)
{
	validatePDBID(pdbID);

	UpdateStatus status;

	auto data = getData(pdbID);
	if (data and data["properties"])
	{
		auto v = data["properties"]["VERSION"].get<float>();
		status.ok = v >= version();
	}

	pqxx::transaction tx(prsm_db_connection::instance());
	auto row = tx.exec(R"(SELECT MAX(version) FROM redo.update_request WHERE pdb_id = )" + tx.quote(pdbID)).one_row();
	status.pendingVersion = row[0].as<std::optional<float>>();

	return status;
}

void DataService::requestUpdate(const std::string &pdbID, const User &user)
{
	validatePDBID(pdbID);

	pqxx::transaction tx(prsm_db_connection::instance());
	tx.exec(R"(
		INSERT INTO redo.update_request(pdb_id, user_id, version)
		     VALUES ()" +
			tx.quote(pdbID) + ", " + tx.quote(user.id) + ", " + tx.quote(version()) + R"())")
		.no_rows();
	tx.commit();
}

void DataService::deleteUpdateRequest(uint64_t id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	tx.exec(R"(DELETE FROM redo.update_request WHERE id = )" + tx.quote(id)).no_rows();
	tx.commit();
}

std::vector<UpdateRequest> DataService::getAllUpdateRequests()
{
	std::scoped_lock lock(m_mutex);

	std::vector<UpdateRequest> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(R"(SELECT a.*, b.name AS user FROM redo.update_request a JOIN redo.user b ON a.user_id = b.id)");
	for (auto row : rows)
		result.emplace_back(row);

	tx.commit();

	for (auto &req : result)
	{
		auto data = getData(req.pdb_id);
		auto upToDate = data and data["properties"] and data["properties"]["VERSION"].get<float>() >= req.version;

		if (not upToDate) // this entry is still not up-to-date
			continue;

		pqxx::transaction tx1(prsm_db_connection::instance());
		tx1.exec(R"(DELETE FROM redo.update_request WHERE id = )" + tx1.quote(req.id)).no_rows();
		tx1.commit();

		req.id = 0;
	}

	std::erase_if(result, [](UpdateRequest &r)
		{ return r.id == 0; });

	return result;
}

float DataService::version() const
{
	float result = 0;

	std::ifstream version_file(m_data_dir / "redo-version.txt");
	if (version_file.is_open())
	{
		std::string line;
		getline(version_file, line);

		result = std::stof(line);
		// auto r = std::from_chars(line.data(), line.data() + line.length(), result);
		// if (r.ec != std::errc())
		// 	std::cerr << "Error converting version from redo-version.txt\n";
	}

	return result;
}

std::filesystem::path DataService::getSubdir(std::string_view pdbID) const
{
	validatePDBID(pdbID);
	return m_data_dir / pdbID.substr(pdbID.length() - 3, 2);
}

bool DataService::exists(const std::string &pdbID) const
{
	validatePDBID(pdbID);
	auto entry_dir = getSubdir(pdbID) / pdbID;

	std::error_code ec;
	return fs::is_directory(entry_dir, ec);
}

std::string DataService::getWhyNot(const std::string &pdbID)
{
	validatePDBID(pdbID);

	std::string whynot("The PDB-REDO entry is being created");

	std::ifstream whyNotFile(m_data_dir / "whynot" / (pdbID + ".txt"));
	if (whyNotFile.is_open())
	{
		getline(whyNotFile, whynot);

		if (zeep::starts_with(whynot, "COMMENT: "))
			whynot.erase(0, 9);
	}
	else
	{
		auto &config = mcfp::config::instance();

		auto uri = config.get("ebi-coord-template");
		for (auto i = uri.find("${id}"); i != std::string::npos; i = uri.find("${id}", i))
			uri.replace(i, 5, pdbID);

		if (not zeep::http::head_request(uri))
			whynot = "PDB Entry does not exist";
		else
		{
			auto uri = config.get("ebi-sf-template");
			for (auto i = uri.find("${id}"); i != std::string::npos; i = uri.find("${id}", i))
				uri.replace(i, 5, pdbID);

			if (not zeep::http::head_request(uri))
				whynot = "No reflection data available";
		}
	}

	return whynot;
}

std::string DataService::getLatestAttic(const std::string &pdbID)
{
	validatePDBID(pdbID);

	using namespace std::chrono;

	std::string result;

	auto attic_dir = getSubdir(pdbID) / pdbID / "attic";

	system_clock::time_point t{};

	std::error_code ec;
	if (fs::exists(attic_dir, ec))
	{
		for (auto di = fs::directory_iterator(attic_dir); di != fs::directory_iterator(); ++di)
		{
			auto dt = fs::last_write_time(di->path(), ec);
			auto dd = time_point_cast<system_clock::duration>(dt - decltype(dt)::clock::now() + system_clock::now());

			if (t < dd)
			{
				t = dd;
				result = di->path().filename().string();
			}
		}
	}

	return result;
}

std::vector<std::string> DataService::getFileList(const std::string &pdbID, const std::optional<std::string> &attic)
{
	validatePDBID(pdbID);

	auto entry_dir = getSubdir(pdbID) / pdbID;
	if (attic)
		entry_dir = sanitizePath(entry_dir / "attic", *attic);

	if (not fs::exists(entry_dir))
		throw std::system_error(zeep::http::status_type::not_found);

	std::vector<std::string> result;
	for (const auto &f : fs::recursive_directory_iterator(entry_dir))
	{
		if (not f.is_regular_file())
			continue;

		result.push_back(fs::relative(f.path(), entry_dir).string());
	}

	return result;
}

std::filesystem::path DataService::getFile(const std::string &pdbID, const std::string &file, const std::optional<std::string> &attic)
{
	validatePDBID(pdbID);

	auto entry_dir = getSubdir(pdbID) / pdbID;
	if (attic)
		entry_dir = sanitizePath(entry_dir / "attic", *attic);

	if (not fs::exists(entry_dir))
		throw std::system_error(zeep::http::status_type::not_found);

	return entry_dir / file;
}

zeep::el::object DataService::getData(const std::string &pdbID, const std::optional<std::string> &attic)
{
	validatePDBID(pdbID);

	zeep::el::object data;

	auto entry_dir = getSubdir(pdbID) / pdbID;
	if (attic)
		entry_dir = sanitizePath(entry_dir / "attic", *attic);

	if (fs::exists(entry_dir))
	{
		fs::path p = entry_dir / "data.json";

		if (fs::exists(p))
		{
			std::ifstream file(p);
			if (file.is_open())
				data = zeep::el::object::parse_JSON(file);
		}

		p = entry_dir / "versions.json";
		if (fs::exists(p))
		{
			std::ifstream file(p);
			if (file.is_open())
				data["_versions"] = zeep::el::object::parse_JSON(file);
		}
	}

	return data;
}

std::tuple<std::unique_ptr<std::istream>, std::string> DataService::getZipFile(const std::string &pdbID, const std::optional<std::string> &attic)
{
	validatePDBID(pdbID);

	auto entry_dir = getSubdir(pdbID) / pdbID;
	if (attic)
		entry_dir = sanitizePath(entry_dir / "attic", *attic);

	if (not fs::exists(entry_dir))
		throw std::system_error(zeep::http::status_type::not_found);

	ZipWriter zw;

	fs::path d(pdbID);

	for (const auto &f : fs::directory_iterator(entry_dir))
	{
		if (f.path().filename() == "attic")
			continue;

		if (f.is_regular_file())
			zw.add(f.path(), (d / fs::relative(f.path(), entry_dir)).string());
		else if (f.is_directory())
		{
			for (const auto &fr : fs::directory_iterator(f.path()))
			{
				if (not fr.is_regular_file())
					continue;

				zw.add(fr.path(), (d / fs::relative(fr.path(), entry_dir)).string());
			}
		}
	}

	return { zw.finish(), pdbID + ".zip" };
}
