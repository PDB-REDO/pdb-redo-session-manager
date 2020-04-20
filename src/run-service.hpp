//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <memory>

#include <zeep/el/element.hpp>

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

std::string to_string(RunStatus status);
RunStatus from_string(const std::string& status);

// --------------------------------------------------------------------

struct Run
{
	uint32_t id;
	std::string user;
	RunStatus status;

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

	Run submit(const std::string& user, const std::string& pdb, const std::string& mtz,
		const std::string& restraints, const std::string& sequence, const zeep::el::element& params);

  private:

	RunService(const std::string& runsDir);

	static std::unique_ptr<RunService> sInstance;
	std::string m_runsdir;
};