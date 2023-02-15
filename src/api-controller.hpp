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

#include "run-service.hpp"
#include "session-service.hpp"

#include <zeep/http/rest-controller.hpp>

// --------------------------------------------------------------------

class APIRESTController : public zeep::http::rest_controller
{
  public:
	APIRESTController();

	// Bottle neck, to validate access tokens on requests
	virtual bool handle_request(zeep::http::request &req, zeep::http::reply &rep);

	// CRUD routines

	CreateSessionResult getSession(unsigned long id);
	void deleteSession(unsigned long id);
	std::vector<Run> getAllRuns(unsigned long id);

	Run createJob(unsigned long sessionID, const zeep::http::file_param &diffractionData, const zeep::http::file_param &coordinates,
		const zeep::http::file_param &restraints, const zeep::http::file_param &sequence, const zeep::json::element &params);

	Run getRun(unsigned long sessionID, unsigned long runID);

	std::vector<std::string> getResultFileList(unsigned long sessionID, unsigned long runID);

	std::filesystem::path getResultFile(unsigned long sessionID, unsigned long runID, const std::string &file);

	zeep::http::reply getZippedResultFile(unsigned long sessionID, unsigned long runID);

	void deleteRun(unsigned long sessionID, unsigned long runID);

  private:
	std::filesystem::path m_pdb_redo_dir;
};