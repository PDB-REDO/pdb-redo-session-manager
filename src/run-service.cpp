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

#include <cassert>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>

#include <zeep/streambuf.hpp>
#include <zeep/json/parser.hpp>

#include "run-service.hpp"
#include "user-service.hpp"
#include "zip-support.hpp"

namespace fs = std::filesystem;
namespace zh = zeep::http;

// --------------------------------------------------------------------

std::string to_string(RunStatus status)
{
	switch (status)
	{
		case RunStatus::UNDEFINED:
			return "undefined";
		case RunStatus::REGISTERED:
			return "registered";
		case RunStatus::STARTING:
			return "starting";
		case RunStatus::QUEUED:
			return "queued";
		case RunStatus::RUNNING:
			return "running";
		case RunStatus::STOPPING:
			return "stopping";
		case RunStatus::STOPPED:
			return "stopped";
		case RunStatus::ENDED:
			return "ended";
		case RunStatus::DELETING:
			return "deleting";
	}
	throw std::runtime_error("Invalid status value");
}

RunStatus from_string(const std::string &status)
{
	if (status == "undefined")
		return RunStatus::UNDEFINED;
	if (status == "registered")
		return RunStatus::REGISTERED;
	if (status == "starting")
		return RunStatus::STARTING;
	if (status == "queued")
		return RunStatus::QUEUED;
	if (status == "running")
		return RunStatus::RUNNING;
	if (status == "stopping")
		return RunStatus::STOPPING;
	if (status == "stopped")
		return RunStatus::STOPPED;
	if (status == "ended")
		return RunStatus::ENDED;
	if (status == "deleting")
		return RunStatus::DELETING;
	throw std::runtime_error("Invalid status");
}

// --------------------------------------------------------------------

static const std::regex kRunDirNameRx(R"([0-9]{10})");

// --------------------------------------------------------------------

Run Run::create(const fs::path &dir, const std::string &username)
{
	using namespace std::chrono;

	Run run;

	run.m_dir = dir;
	run.id = std::stoul(dir.filename().string());
	run.user = username;

	run.status = RunStatus::REGISTERED;
	if (fs::exists(dir / "startingProcess.txt"))
	{
		run.status = RunStatus::STARTING;

		auto ft = fs::last_write_time(dir / "startingProcess.txt");
		run.started = time_point_cast<system_clock::duration>(ft - decltype(ft)::clock::now() + system_clock::now());
	}
	if (fs::exists(dir / "rank.txt"))
		run.status = RunStatus::QUEUED;
	if (fs::exists(dir / "processRunning.txt"))
		run.status = RunStatus::RUNNING;
	if (fs::exists(dir / "stoppingProcess.txt"))
		run.status = RunStatus::STOPPING;
	if (fs::exists(dir / "processStopped.txt"))
		run.status = RunStatus::STOPPED;
	if (fs::exists(dir / "processEnded.txt"))
		run.status = RunStatus::ENDED;
	if (fs::exists(dir / "deletingProcess.txt"))
		run.status = RunStatus::DELETING;

	run.has_image = fs::exists(dir / "pdbin.png");

	auto ft = fs::last_write_time(dir);
    run.date = time_point_cast<system_clock::duration>(ft - decltype(ft)::clock::now() + system_clock::now());

	if (fs::is_directory(dir / "input"))
	{
		for (auto d : fs::directory_iterator(dir / "input"))
		{
			for (auto f : fs::directory_iterator(d))
				run.input.emplace_back(f.path().filename().string());
		}
	}

	// load the scores
	std::ifstream scoreFile(dir / "output" / "pdbe.json");
	if (scoreFile.is_open())
	{
		zeep::json::element score;
		zeep::json::parse_json(scoreFile, score);

		Score v;
		from_element(score, v);

		double range = v.ddatafit.rangeUpper - v.ddatafit.rangeLower;
		double dDataFit = (v.ddatafit.zdfree - v.ddatafit.rangeLower) / range;

		v.ddatafit.position = static_cast<int>(std::ceil(dDataFit * 5) - 1);
		if (v.ddatafit.position < 0)
			v.ddatafit.position = 0;
		if (v.ddatafit.position > 4)
			v.ddatafit.position = 4;

		if (v.proteinGeometry.has_value())
		{
			double range = v.proteinGeometry->rangeUpper - v.proteinGeometry->rangeLower;
			double geometry = (v.proteinGeometry->dzscore - v.proteinGeometry->rangeLower) / range;

			v.proteinGeometry->position = static_cast<int>(std::ceil(geometry * 5) - 1);
			if (v.proteinGeometry->position < 0)
				v.proteinGeometry->position = 0;
			if (v.proteinGeometry->position > 4)
				v.proteinGeometry->position = 4;
		}

		if (v.nucleicAcidGeometry.has_value())
		{
			double range = v.nucleicAcidGeometry->rangeUpper - v.nucleicAcidGeometry->rangeLower;
			double geometry = (v.nucleicAcidGeometry->drmsz - v.nucleicAcidGeometry->rangeLower) / range;

			v.nucleicAcidGeometry->position = static_cast<int>(std::ceil(geometry * 5) - 1);
			if (v.nucleicAcidGeometry->position < 0)
				v.nucleicAcidGeometry->position = 0;
			if (v.nucleicAcidGeometry->position > 4)
				v.nucleicAcidGeometry->position = 4;
		}

		run.score = v;
	}

	return run;
}

std::vector<std::string> Run::getResultFileList()
{
	if (not fs::exists(m_dir))
		throw std::runtime_error("Run directory does not exist");

	fs::path output = m_dir / "output";

	if (not fs::exists(output))
		throw std::runtime_error("Result directory does not exist");

	std::vector<std::string> result;
	for (auto f : fs::recursive_directory_iterator(output))
	{
		if (not f.is_regular_file())
			continue;

		result.push_back(fs::relative(f.path(), output).string());
	}

	return result;
}

std::filesystem::path Run::getResultFile(const std::string& file)
{
	if (not fs::exists(m_dir))
		throw std::runtime_error("Run directory does not exist");

	return m_dir / "output" / file;
}

std::filesystem::path Run::getImageFile()
{
	if (not fs::exists(m_dir))
		throw std::runtime_error("Run does not exist");

	return m_dir / "pdbin.png";
}

std::tuple<std::istream *, std::string> Run::getZippedResultFile()
{
	if (not fs::exists(m_dir))
		throw std::runtime_error("Run does not exist");

	std::ostringstream s;
	s << std::setw(10) << std::setfill('0') << id;

	fs::path output = m_dir / "output";
	fs::path d(s.str());

	if (not fs::exists(output))
		throw std::runtime_error("Result directory does not exist");

	ZipWriter zw;

	for (auto f : fs::recursive_directory_iterator(output))
	{
		if (not f.is_regular_file())
			continue;
		
		zw.add(f.path(), (d / fs::relative(f.path(), output)).string());
	}

	return { zw.finish(), s.str() + ".zip" };
}

// --------------------------------------------------------------------

std::unique_ptr<RunService> RunService::s_instance;

RunService::RunService(const std::string &runsDir)
	: m_runsdir(runsDir)
{
	zeep::value_serializer<RunStatus>::instance("RunStatus")
		("undefined", RunStatus::UNDEFINED)
		("registered", RunStatus::REGISTERED)
		("starting", RunStatus::STARTING)
		("queued", RunStatus::QUEUED)
		("running", RunStatus::RUNNING)
		("stopping", RunStatus::STOPPING)
		("stopped", RunStatus::STOPPED)
		("ended", RunStatus::ENDED)
		("deleting", RunStatus::DELETING);
}

void RunService::init(const std::string &runsDir)
{
	assert(not s_instance);

	s_instance.reset(new RunService(runsDir));
}

RunService &RunService::instance()
{
	assert(s_instance);
	return *s_instance;
}

Run RunService::submit(const std::string &user, const zh::file_param &pdb, const zh::file_param &mtz,
	const zh::file_param &restraints, const zh::file_param &sequence, const zeep::json::element &params)
{
	using namespace std::literals;

	const std::regex rx("[-a-zA-Z0-9+_().]+");

	// create user directory first, if needed
	auto userDir = m_runsdir / user;
	if (not fs::exists(userDir))
		fs::create_directories(userDir);

	auto runID = UserService::instance().createRunID(user);

	Run run{ m_runsdir, runID, user, RunStatus::REGISTERED };

	std::ostringstream s;
	s << std::setw(10) << std::setfill('0') << runID;

	auto runDir = userDir / s.str();

	if (fs::exists(runDir))
		throw std::runtime_error("Internal error: run dir already exists");

	fs::create_directory(runDir);
	fs::create_directory(runDir / "output");

	std::pair<const char *, const zh::file_param &> files[] = {
		{ "PDB", pdb }, { "MTZ", mtz }, { "CIF", restraints }, { "SEQ", sequence }
	};

	std::ofstream info(runDir / "info.txt");

	auto v_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::ostringstream ss;
	info << std::put_time(std::localtime(&v_t), "[%F]");

	for (auto &&[type, file] : files)
	{
		if (not file or file.length == 0)
			continue;

		zeep::char_streambuf sb(file.data, file.length);

		gxrio::istream in(&sb);

		fs::path input = std::regex_match(file.filename, rx) ? file.filename : "input."s + type;

		if (input.extension() == ".gz")
			input = input.stem();

		auto dir = runDir / "input" / type;
		fs::create_directories(dir);

		std::ofstream out(dir / input, std::ios::binary);

		out << in.rdbuf();

		info << ':' << type << '=' << input;
	}

	// write parameters;

	if (not params.is_null())
	{
		// backward compatible
		info << ":PAIRED=" << (params["paired"] ? 1 : 0);

		std::ofstream paramsFile(runDir / "params.json");
		paramsFile << params;
	}

	info << std::endl;

	// create a flag to start processing
	std::ofstream start(runDir / "startingProcess.txt");

	run.status = RunStatus::STARTING;

	return run;
}

std::vector<Run> RunService::getRunsForUser(const std::string &username)
{
	std::vector<Run> result;

	auto dir = m_runsdir / username;

	if (fs::exists(dir))
	{
		for (auto i = fs::directory_iterator(dir); i != fs::directory_iterator(); ++i)
		{
			if (not i->is_directory())
				continue;

			// auto name = i->path().filename().string();
			if (not std::regex_match(i->path().filename().string(), kRunDirNameRx))
				continue;

			if (not fs::is_directory(i->path() / "input"))
				continue;

			try
			{
				result.push_back(Run::create(i->path(), username));
			}
			catch (const std::exception &e)
			{
				std::cerr << e.what() << std::endl;
			}
		}
	}

	std::sort(result.begin(), result.end(), [](Run &a, Run &b)
		{ return a.id < b.id; });

	return result;
}

Run RunService::getRun(const std::string &username, unsigned long runID)
{
	Run result;

	auto dir = m_runsdir / username;

	if (fs::exists(dir))
	{
		std::ostringstream s;
		s << std::setw(10) << std::setfill('0') << runID;

		if (fs::exists(dir / s.str()))
			result = Run::create(dir / s.str(), username);
	}

	return result;
}

std::vector<Run> RunService::getAllRuns()
{
	std::vector<Run> result;

	for (auto userdir = fs::directory_iterator(m_runsdir); userdir != fs::directory_iterator(); ++userdir)
	{
		if (not userdir->is_directory())
			continue;
		
		for (auto i = fs::directory_iterator(*userdir); i != fs::directory_iterator(); ++i)
		{
			if (not i->is_directory())
				continue;

			// auto name = i->path().filename().string();
			if (not std::regex_match(i->path().filename().string(), kRunDirNameRx))
				continue;

			if (not fs::is_directory(i->path() / "input"))
				continue;

			try
			{
				result.push_back(Run::create(i->path(), userdir->path().filename().string()));
			}
			catch (const std::exception &e)
			{
				std::cerr << e.what() << std::endl;
			}
		}
	}

	std::sort(result.begin(), result.end(), [](Run &a, Run &b)
		{ return a.date > b.date; });

	return result;
}

// --------------------------------------------------------------------

void RunService::deleteRun(const std::string &username, unsigned long runID)
{
	auto dir = m_runsdir / username;

	if (not fs::exists(dir))
		throw std::runtime_error("Run does not exist");

	std::ostringstream s;
	s << std::setw(10) << std::setfill('0') << runID;

	fs::path rundir = dir / s.str();

	fs::remove_all(rundir);
}