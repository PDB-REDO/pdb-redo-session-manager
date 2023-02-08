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

#include <zeep/http/rest-controller.hpp>

#include <pqxx/pqxx>

struct Session
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string token;
	std::chrono::time_point<std::chrono::system_clock> created;
	std::chrono::time_point<std::chrono::system_clock> expires;

	Session &operator=(const pqxx::row &row);

	operator bool() const { return id != 0; }

	bool expired() const { return std::chrono::system_clock::now() > expires; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("expires", expires);
	}
};

struct CreateSessionResult
{
	unsigned long id = 0;
	std::string name;
	std::string token;
	std::chrono::time_point<std::chrono::system_clock> expires;

	CreateSessionResult(const Session &session);
	CreateSessionResult(const pqxx::row &row);
	CreateSessionResult &operator=(const Session &session);
	CreateSessionResult &operator=(const pqxx::row &row);

	operator bool() const { return id != 0; }

	bool expired() const { return std::chrono::system_clock::now() > expires; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("expires", expires);
	}
};

// --------------------------------------------------------------------

class SessionStore
{
  public:
	static void init()
	{
		sInstance = new SessionStore();
	}

	static SessionStore &instance()
	{
		return *sInstance;
	}

	void stop()
	{
		delete sInstance;
		sInstance = nullptr;
	}

	Session create(const std::string &name, const std::string &user);

	Session get_by_id(unsigned long id);
	Session get_by_token(const std::string &token);
	void delete_by_id(unsigned long id);

	std::vector<Session> get_all_sessions();

  private:
	SessionStore();
	~SessionStore();

	SessionStore(const SessionStore &) = delete;
	SessionStore &operator=(const SessionStore &) = delete;

	void run_clean_thread();

	bool m_done = false;

	std::condition_variable m_cv;
	std::mutex m_cv_m;
	std::thread m_clean;

	static SessionStore *sInstance;
};

// --------------------------------------------------------------------

class SessionRESTController : public zeep::http::rest_controller
{
  public:
	SessionRESTController();

	// CRUD routines
	CreateSessionResult post_session(std::string user, std::string password, std::string name);

	// A simple version, requires user to be logged in
	CreateSessionResult get_session(std::string name);
};
