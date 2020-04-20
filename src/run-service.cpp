//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <stdexcept>
#include <cassert>

#include "run-service.hpp"

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

std::unique_ptr<RunService> RunService::sInstance;

RunService::RunService(const std::string& runsDir)
	: m_runsdir(runsDir)
{

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

Run RunService::submit(const std::string& user, const std::string& pdb, const std::string& mtz,
		const std::string& restraints, const std::string& sequence, const zeep::el::element& params)
{
	return { 1, user, RunStatus::STARTING };
}

