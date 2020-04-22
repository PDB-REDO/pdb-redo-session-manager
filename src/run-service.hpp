//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <memory>
#include <filesystem>

#include <zeep/el/element.hpp>
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
		const zeep::http::file_param& restraints, const zeep::http::file_param& sequence, const zeep::el::element& params);

	std::vector<Run> get_runs_for_user(const std::string& username);
	Run get_run(const std::string& username, unsigned long runID);

	std::filesystem::path get_result_file(const std::string& username, unsigned long runID, const std::string& file);

  private:

	RunService(const std::string& runsDir);

	static std::unique_ptr<RunService> sInstance;
	std::filesystem::path m_runsdir;
};