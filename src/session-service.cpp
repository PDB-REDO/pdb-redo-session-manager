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

#include "session-service.hpp"

#include "prsm-db-connection.hpp"
#include "user-service.hpp"

#include <iostream>

// --------------------------------------------------------------------

SessionService *SessionService::sInstance = nullptr;

SessionService::SessionService()
	: m_clean(std::bind(&SessionService::runCleanThread, this))
{
}

SessionService::~SessionService()
{
	{
		std::lock_guard<std::mutex> lock(m_cv_m);
		m_done = true;
		m_cv.notify_one();
	}

	m_clean.join();
}

void SessionService::runCleanThread()
{
	while (not m_done)
	{
		std::unique_lock<std::mutex> lock(m_cv_m);
		if (m_cv.wait_for(lock, std::chrono::seconds(60)) == std::cv_status::timeout)
		{
			try
			{
				pqxx::transaction tx(prsm_db_connection::instance());
				auto r = tx.exec0(R"(DELETE FROM redo.session WHERE CURRENT_TIMESTAMP > expires)");
				tx.commit();
			}
			catch (const std::exception &ex)
			{
				std::cerr << ex.what() << std::endl;
			}
		}
	}
}

Session SessionService::create(const std::string &name, const std::string &user)
{
	using namespace date;

	User u = UserService::instance().getUser(user);

	pqxx::transaction tx(prsm_db_connection::instance());

	std::string token = zeep::encode_base64url(zeep::random_hash());

	auto r = tx.exec1(
		R"(INSERT INTO redo.session (user_id, name, token)
		   VALUES ()" + std::to_string(u.id) + ", " + tx.quote(name) + ", " + tx.quote(token) + R"()
		   RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	unsigned long tokenid = r[0].as<unsigned long>();
	std::string created{ r["created"].as<std::string>() };
	std::string expires{ r["expires"].as<std::string>() };

	tx.commit();

	return {
		tokenid,
		name,
		user,
		token,
		parse_timestamp(created),
		parse_timestamp(expires)
	};
}

Session SessionService::getSessionByID(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM redo.session a LEFT JOIN redo.user b ON a.user_id = b.id
		   WHERE a.id = )" + tx.quote(id));

	Session result(r);

	tx.commit();

	return result;
}

void SessionService::deleteSession(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec0(R"(DELETE FROM redo.session WHERE id = )" + std::to_string(id));
	tx.commit();
}

std::vector<Session> SessionService::getAllSessions()
{
	std::vector<Session> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(
		R"(SELECT a.id AS id,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires,
				  a.name AS name,
				  b.name AS user,
				  a.token
			 FROM redo.session a, redo.user b
			WHERE a.user_id = b.id
			ORDER BY a.created ASC)");

	for (auto row : rows)
		result.emplace_back(row);

	return result;
}

std::vector<Session> SessionService::getAllSessionsForUser(const std::string &username)
{
	std::vector<Session> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(
		R"(SELECT a.id AS id,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires,
				  a.name AS name,
				  b.name AS user,
				  a.token
			 FROM redo.session a, redo.user b
			WHERE a.user_id = b.id AND b.name = )" + tx.quote(username) + R"(
			ORDER BY a.created ASC)");

	for (auto row : rows)
		result.emplace_back(row);

	return result;
}

// --------------------------------------------------------------------

Session::Session(unsigned long id, std::string name, const std::string &user, const std::string &token,
	std::chrono::time_point<std::chrono::system_clock> created, std::chrono::time_point<std::chrono::system_clock> expires)
	: id(id)
	, user(user)
	, token(token)
	, created(created)
	, expires(expires)
{
}

Session::Session(const pqxx::row &row)
	: id(row.at("id").as<unsigned long>())
	, name(row.at("name").as<std::string>())
	, user(row.at("user").as<std::string>())
	, token(row.at("token").as<std::string>())
	, created(parse_timestamp(row.at("created").as<std::string>()))
	, expires(parse_timestamp(row.at("expires").as<std::string>()))
{
}

Session &Session::operator=(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	user = row.at("user").as<std::string>();
	token = row.at("token").as<std::string>();
	created = parse_timestamp(row.at("created").as<std::string>());
	expires = parse_timestamp(row.at("expires").as<std::string>());

	return *this;
}

// --------------------------------------------------------------------

CreateSessionResult::CreateSessionResult(const Session &session)
	: id(session.id)
	, name(session.name)
	, token(session.token)
	, expires(session.expires)
{
}

CreateSessionResult::CreateSessionResult(const pqxx::row &row)
	: id(row.at("id").as<unsigned long>())
	, name(row.at("name").as<std::string>())
	, token(row.at("token").as<std::string>())
	, expires(parse_timestamp(row.at("expires").as<std::string>()))
{
}

CreateSessionResult &CreateSessionResult::operator=(const Session &session)
{
	id = session.id;
	name = session.name;
	token = session.token;
	expires = session.expires;

	return *this;
}

CreateSessionResult &CreateSessionResult::operator=(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	token = row.at("token").as<std::string>();
	expires = parse_timestamp(row.at("expires").as<std::string>());

	return *this;
}

SessionRESTController::SessionRESTController()
	: zeep::http::rest_controller("session")
{
	// create a new session, user should provide username, password and session name
	map_post_request("", &SessionRESTController::postSession, "user", "password", "name");
}

// CRUD routines
CreateSessionResult SessionRESTController::postSession(std::string user, std::string password, std::string name)
{
	User u = UserService::instance().getUser(user);
	std::string pw = u.password;

	PasswordEncoder pwenc;

	if (not pwenc.matches(password, u.password))
		throw std::runtime_error("Invalid username/password");

	if (name.empty())
		name = "<untitled>";

	return SessionService::instance().create(name, user);
}