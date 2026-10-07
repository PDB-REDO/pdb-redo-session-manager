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

#include <filesystem>
#include <memory>
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

struct DDataFit
{
	double zdfree;
	double rangeLower;
	double rangeUpper;
	int position;

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("zdfree", zdfree)
		   & zeem::name_value_pair("range-lower", rangeLower)
		   & zeem::name_value_pair("range-upper", rangeUpper)
		   & zeem::name_value_pair("position", position);
		// clang-format on
	}
};

struct ProteinGeometry
{
	double dzscore;
	double rangeLower;
	double rangeUpper;
	int position;

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("dzscore", dzscore)
		   & zeem::name_value_pair("range-lower", rangeLower)
		   & zeem::name_value_pair("range-upper", rangeUpper)
		   & zeem::name_value_pair("position", position);
		// clang-format on
	}
};

struct NucleicAcidGeometry
{
	double drmsz;
	double rangeLower;
	double rangeUpper;
	int position;

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("drmsz", drmsz)
		   & zeem::name_value_pair("range-lower", rangeLower)
		   & zeem::name_value_pair("range-upper", rangeUpper)
		   & zeem::name_value_pair("position", position);
		// clang-format on
	}
};

struct Score
{
	DDataFit ddatafit;
	std::optional<ProteinGeometry> proteinGeometry;
	std::optional<NucleicAcidGeometry> nucleicAcidGeometry;

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("ddatafit", ddatafit)
		   & zeem::name_value_pair("geometry", proteinGeometry)
		   & zeem::name_value_pair("basePairs", nucleicAcidGeometry);
		// clang-format on
	}
};

struct Run
{
	std::filesystem::path m_dir;

	uint32_t id{};
	std::string user;
	RunStatus status{};
	bool has_image{};
	std::chrono::time_point<std::chrono::system_clock> date;
	std::optional<std::chrono::time_point<std::chrono::system_clock>> started;
	std::optional<Score> score;
	std::vector<std::string> input;

	static Run create(const std::filesystem::path &dir, const std::string &username);

	std::vector<std::string> getResultFileList();
	std::filesystem::path getResultFile(const std::string &file);
	std::filesystem::path getImageFile();
	std::tuple<std::unique_ptr<std::istream>, std::string> getZippedResultFile();

	template <typename Archive>
	void serialize(Archive &ar, uint64_t /*version*/)
	{
		// clang-format off
		ar & zeem::name_value_pair("id", id)
		   & zeem::name_value_pair("user", user)
		   & zeem::name_value_pair("status", status)
		   & zeem::name_value_pair("has-image", has_image)
		   & zeem::name_value_pair("date", date)
		   & zeem::name_value_pair("started-date", started)
		   & zeem::name_value_pair("score", score)
		   & zeem::name_value_pair("input", input);
		// clang-format on
	}
};

class RunService
{
  public:
	static void init(const std::string &runsDir);
	static RunService &instance();

	RunService(const RunService &) = delete;
	RunService &operator=(const RunService &) = delete;

	Run submit(const std::string &user, const zeep::http::file_param &pdb, const zeep::http::file_param &mtz,
		const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, const zeep::el::object &params);

	std::vector<Run> getRunsForUser(const std::string &username);
	Run getRun(const std::string &username, uint64_t runID);
	std::vector<Run> getAllRuns();

	// add a clean up routine
	void deleteRun(const std::string &username, uint64_t runID);

	[[nodiscard]] std::filesystem::path getRunsDir() const { return m_runsdir; }

  private:
	explicit RunService(const std::string &runsDir);

	static std::unique_ptr<RunService> s_instance;
	std::filesystem::path m_runsdir;
};