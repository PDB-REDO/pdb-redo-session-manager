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

#include <mcfp.hpp>

#include <zeep/http/reply.hpp>

#include "data-service.hpp"
#include "zip-support.hpp"

namespace fs = std::filesystem;

data_service &data_service::instance()
{
	static data_service s_instance;
	return s_instance;
}

data_service::data_service()
{
	auto &config = mcfp::config::instance();

	m_data_dir = config.get<std::string>("pdb-redo-db-dir");
	if (not fs::exists(m_data_dir))
		throw std::runtime_error("PDB-REDO data directory (" + m_data_dir.string() + ") does not exists");
}

std::vector<std::string> data_service::get_file_list(const std::string &pdbID)
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

std::filesystem::path data_service::get_file(const std::string &pdbID, const std::string& file)
{
	auto entry_dir = m_data_dir / pdbID.substr(1, 2) / pdbID;
	if (not fs::exists(entry_dir))
		throw zeep::http::not_found;

	fs::path result = entry_dir / file;

	if (not fs::exists(result))
		throw std::runtime_error("Result file does not exist");

	return result;
}

std::tuple<std::istream *, std::string> data_service::get_zip_file(const std::string &pdbID)
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

std::string data_service::version() const
{
	std::string result = "<unknown>";

	std::ifstream version_file(m_data_dir / "redo-version.txt");
	if (version_file.is_open())
		getline(version_file, result);
	
	return result;
}
