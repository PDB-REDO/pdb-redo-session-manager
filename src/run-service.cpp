//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <stdexcept>
#include <cassert>
#include <regex>

#include "run-service.hpp"
#include "user-service.hpp"

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
}

RunStatus from_string(const std::string& status)
{
	if (status == "undefined")		return RunStatus::UNDEFINED;
	if (status == "registered")		return RunStatus::REGISTERED;
	if (status == "starting")		return RunStatus::STARTING;
	if (status == "queued")			return RunStatus::QUEUED;
	if (status == "running")		return RunStatus::RUNNING;
	if (status == "stopping")		return RunStatus::STOPPING;
	if (status == "stopped")		return RunStatus::STOPPED;
	if (status == "ended")			return RunStatus::ENDED;
	if (status == "deleting")		return RunStatus::DELETING;
	throw std::runtime_error("Invalid status");
}

// --------------------------------------------------------------------

static const std::regex kRunDirNameRx(R"([0-9]{10})");

// --------------------------------------------------------------------

Run Run::create(const fs::path& dir, const std::string& username)
{
	Run run;

	run.id = std::stoul(dir.filename().string());
	run.user = username;
	
	run.status = RunStatus::REGISTERED;
	if (fs::exists(dir / "startingProcess.txt"))	run.status = RunStatus::STARTING;
	if (fs::exists(dir / "rank.txt"))				run.status = RunStatus::QUEUED;
	if (fs::exists(dir / "processRunning.txt"))		run.status = RunStatus::RUNNING;
	if (fs::exists(dir / "stoppingProcess.txt"))	run.status = RunStatus::STOPPING;
	if (fs::exists(dir / "processStopped.txt"))		run.status = RunStatus::STOPPED;
	if (fs::exists(dir / "processEnded.txt"))		run.status = RunStatus::ENDED;
	if (fs::exists(dir / "deletingProcess.txt"))	run.status = RunStatus::DELETING;

	return run;
}

// --------------------------------------------------------------------

std::unique_ptr<RunService> RunService::sInstance;

RunService::RunService(const std::string& runsDir)
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

void RunService::init(const std::string& runsDir)
{
	assert(not sInstance);

	sInstance.reset(new RunService(runsDir));
}

RunService& RunService::instance()
{
	assert(sInstance);
	return *sInstance;
}

Run RunService::submit(const std::string& user, const zh::file_param& pdb, const zh::file_param& mtz,
	const zh::file_param& restraints, const zh::file_param& sequence, const zeep::el::element& params)
{
	// create user directory first, if needed
	auto userDir = m_runsdir / user;
	if (not fs::exists(userDir))
		fs::create_directories(userDir);
	
	auto runID = UserService::instance().create_run_id(user);

	Run run{ runID, user, RunStatus::REGISTERED };
	
	std::ostringstream s;
	s << std::setw(10) << std::setfill('0') << runID;

	auto runDir = userDir / s.str();

	if (fs::exists(runDir))
		throw std::runtime_error("Internal error: run dir already exists");
	
	fs::create_directory(runDir);
	fs::create_directory(runDir / "output");
	fs::create_directory(runDir / "input");

	// for (auto&& [type, file]: { { "PDB", pdb }, { "MTZ", mtz }, { "CIF", restraints }, { "SEQ", sequence }})
	// {
	// 	fs::create_directory(runDir / "input" / type);


	// }



	return run;
}

std::vector<Run> RunService::get_runs_for_user(const std::string& username)
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
			catch(const std::exception& e)
			{
				std::cerr << e.what() << std::endl;
			}
		}
	}

	std::sort(result.begin(), result.end(), [](Run& a, Run& b) { return a.id < b.id; });

	return result;
}
