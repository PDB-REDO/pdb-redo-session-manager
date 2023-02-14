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
#include "zip-support.hpp"
#include "prsm-db-connection.hpp"

#include <mcfp.hpp>

#include <zeep/http/reply.hpp>

#include <charconv>
#include <iostream>

namespace fs = std::filesystem;

// --------------------------------------------------------------------

UpdateRequest::UpdateRequest(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
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

std::vector<std::string> DataService::getFileList(const std::string &pdbID)
{
	auto entry_dir = m_data_dir / pdbID.substr(1, 2) / pdbID;
	if (not fs::exists(entry_dir))
		throw zeep::http::not_found;

	std::vector<std::string> result;
	for (auto f : fs::recursive_directory_iterator(entry_dir))
	{
		if (not f.is_regular_file())
			continue;

		result.push_back(fs::relative(f.path(), entry_dir).string());
	}

	return result;
}

std::filesystem::path DataService::getFile(const std::string &pdbID, const std::string& file)
{
	auto entry_dir = m_data_dir / pdbID.substr(1, 2) / pdbID;
	if (not fs::exists(entry_dir))
		throw zeep::http::not_found;

	fs::path result = entry_dir / file;

	if (not fs::exists(result))
		throw std::runtime_error("Result file does not exist");

	return result;
}

zeep::json::element DataService::getData(const std::string &pdbID)
{
	zeep::json::element data;

	auto entry_dir = m_data_dir / pdbID.substr(1, 2) / pdbID;
	if (fs::exists(entry_dir))
	{
		fs::path p = entry_dir / "data.json";

		if (fs::exists(p))
		{
			std::ifstream file(p);
			if (file.is_open())
				zeep::json::parse_json(file, data);
		}
	}

	return data;
}

std::tuple<std::istream *, std::string> DataService::getZipFile(const std::string &pdbID)
{
	auto entry_dir = m_data_dir / pdbID.substr(1, 2) / pdbID;
	if (not fs::exists(entry_dir))
		throw zeep::http::not_found;

	ZipWriter zw;

	fs::path d(pdbID);

	for (auto f : fs::directory_iterator(entry_dir))
	{
		if (f.path().filename() == "attic")
			continue;
		
		if (f.is_regular_file())
			zw.add(f.path(), (d / fs::relative(f.path(), entry_dir)).string());
		else if (f.is_directory())
		{
			for (auto fr : fs::directory_iterator(f.path()))
			{
				if (not fr.is_regular_file())
					continue;
				
				zw.add(fr.path(), (d / fs::relative(fr.path(), entry_dir)).string());
			}
		}
	}

	return { zw.finish(), pdbID + ".zip" };
}

UpdateStatus DataService::getUpdateStatus(const std::string &pdbID)
{
	UpdateStatus status;

	auto data = getData(pdbID);
	if (data and data["properties"])
	{
		auto v = data["properties"]["VERSION"].as<float>();
		status.ok = v >= version();
	}

	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(R"(SELECT MAX(version) FROM public.update_request WHERE pdb_id = )" + tx.quote(pdbID));
	tx.commit();

	if (not r[0].is_null())
		status.pendingVersion = r[0].as<float>();

	return status;
}

void DataService::requestUpdate(const std::string &pdbID, const User &user)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec0(R"(
		INSERT INTO public.update_request(pdb_id, user_id, version)
		     VALUES ()" + tx.quote(pdbID) + ", "
			 	        + tx.quote(user.id) + ", "
						+ tx.quote(version()) + R"())");
	tx.commit();
}

void DataService::deleteUpdateRequest(int id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec0(R"(DELETE FROM public.update_request WHERE id = )" + tx.quote(id));
	tx.commit();
}

std::vector<UpdateRequest> DataService::get_all_update_requests()
{
	std::lock_guard lock(m_mutex);

	std::vector<UpdateRequest> result;

	pqxx::transaction tx(prsm_db_connection::instance());
	auto rows = tx.exec(R"(SELECT a.*, b.name AS user FROM public.update_request a JOIN public.user b ON a.user_id = b.id)");

	for (auto row : rows)
		result.emplace_back(row);

	tx.commit();

	auto pdbRedoVersion = version();

	for (auto &req : result)
	{
		auto data = getData(req.pdb_id);
		auto upToDate = data and data["properties"] and data["properties"]["VERSION"].as<float>() >= pdbRedoVersion;

		if (not upToDate)	// this entry is still not up-to-date
			continue;

		pqxx::transaction tx1(prsm_db_connection::instance());
		tx1.exec0(R"(DELETE FROM public.update_request WHERE id = )" + tx1.quote(req.id));
		tx1.commit();

		req.id = 0;
	}

	result.erase(std::remove_if(result.begin(), result.end(), [](UpdateRequest &r) { return r.id == 0; }), result.end());

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
		// 	std::cerr << "Error converting version from redo-version.txt" << std::endl;
	}
	
	return result;
}
