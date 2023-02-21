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

#include "token-service.hpp"

#include "prsm-db-connection.hpp"
#include "user-service.hpp"

#include <iostream>

// --------------------------------------------------------------------

Token::Token(unsigned long id, std::string name, const std::string &user, const std::string &secret,
	std::chrono::time_point<std::chrono::system_clock> created, std::chrono::time_point<std::chrono::system_clock> expires)
	: id(id)
	, user(user)
	, secret(secret)
	, created(created)
	, expires(expires)
{
}

Token::Token(const pqxx::row &row)
	: id(row.at("id").as<unsigned long>())
	, name(row.at("name").as<std::string>())
	, user(row.at("user").as<std::string>())
	, secret(row.at("secret").as<std::string>())
	, created(parse_timestamp(row.at("created").as<std::string>()))
	, expires(parse_timestamp(row.at("expires").as<std::string>()))
{
}

Token &Token::operator=(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	user = row.at("user").as<std::string>();
	secret = row.at("secret").as<std::string>();
	created = parse_timestamp(row.at("created").as<std::string>());
	expires = parse_timestamp(row.at("expires").as<std::string>());

	return *this;
}

// --------------------------------------------------------------------

TokenService *TokenService::sInstance = nullptr;

TokenService::TokenService()
	: m_clean(std::bind(&TokenService::runCleanThread, this))
{
}

TokenService::~TokenService()
{
	{
		std::lock_guard<std::mutex> lock(m_cv_m);
		m_done = true;
		m_cv.notify_one();
	}

	m_clean.join();
}

void TokenService::runCleanThread()
{
	while (not m_done)
	{
		std::unique_lock<std::mutex> lock(m_cv_m);
		if (m_cv.wait_for(lock, std::chrono::seconds(60)) == std::cv_status::timeout)
		{
			try
			{
				pqxx::transaction tx(prsm_db_connection::instance());
				auto r = tx.exec0(R"(DELETE FROM redo.token WHERE CURRENT_TIMESTAMP > expires)");
				tx.commit();
			}
			catch (const std::exception &ex)
			{
				std::cerr << ex.what() << std::endl;
			}
		}
	}
}

Token TokenService::create(const std::string &name, const std::string &user)
{
	using namespace date;

	User u = UserService::instance().getUser(user);

	pqxx::transaction tx(prsm_db_connection::instance());

	std::string secret = zeep::encode_base64url(zeep::random_hash());

	auto r = tx.exec1(
		R"(INSERT INTO redo.token (user_id, name, secret)
		   VALUES ()" + std::to_string(u.id) + ", " + tx.quote(name) + ", " + tx.quote(secret) + R"()
		   RETURNING id, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	unsigned long tokenid = r[0].as<unsigned long>();
	std::string created{ r[1].as<std::string>() };
	std::string expires{ r[2].as<std::string>() };

	tx.commit();

	return {
		tokenid,
		name,
		user,
		secret,
		parse_timestamp(created),
		parse_timestamp(expires)
	};
}

Token TokenService::getTokenByID(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.secret,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM redo.token a LEFT JOIN redo.user b ON a.user_id = b.id
		   WHERE a.id = )" + tx.quote(id));

	Token result(r);

	tx.commit();

	return result;
}

void TokenService::deleteToken(unsigned long id)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec0(R"(DELETE FROM redo.token WHERE id = )" + std::to_string(id));
	tx.commit();
}

std::vector<Token> TokenService::getAllTokens()
{
	std::vector<Token> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(
		R"(SELECT a.id AS id,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires,
				  a.name AS name,
				  b.name AS user,
				  a.secret
			 FROM redo.token a, redo.user b
			WHERE a.user_id = b.id
			ORDER BY a.created ASC)");

	for (auto row : rows)
		result.emplace_back(row);

	return result;
}

std::vector<Token> TokenService::getAllTokensForUser(const std::string &username)
{
	std::vector<Token> result;

	pqxx::transaction tx(prsm_db_connection::instance());

	auto rows = tx.exec(
		R"(SELECT a.id AS id,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires,
				  a.name AS name,
				  b.name AS user,
				  a.secret
			 FROM redo.token a, redo.user b
			WHERE a.user_id = b.id AND b.name = )" + tx.quote(username) + R"(
			ORDER BY a.created ASC)");

	for (auto row : rows)
		result.emplace_back(row);

	return result;
}
