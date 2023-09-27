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

struct Token
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string secret;
	std::chrono::time_point<std::chrono::system_clock> created;
	std::chrono::time_point<std::chrono::system_clock> expires;

	Token(const pqxx::row &row);
	Token(unsigned long id, std::string name, const std::string &user, const std::string &secret,
		std::chrono::time_point<std::chrono::system_clock> created, std::chrono::time_point<std::chrono::system_clock> expires);
	Token &operator=(const pqxx::row &row);

	explicit operator bool() const { return id != 0; }

	bool expired() const { return std::chrono::system_clock::now() > expires; }

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("secret", secret)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("expires", expires);
	}
};

// --------------------------------------------------------------------

class TokenService
{
  public:
	static void init()
	{
		s_instance = new TokenService();
	}

	static TokenService &instance()
	{
		return *s_instance;
	}

	void stop()
	{
		delete s_instance;
		s_instance = nullptr;
	}

	Token create(const std::string &name, const std::string &user);
	Token getTokenByID(unsigned long id);
	void deleteToken(unsigned long id);

	std::vector<Token> getAllTokens();
	std::vector<Token> getAllTokensForUser(const std::string &user);

  private:
	TokenService();
	~TokenService();

	TokenService(const TokenService &) = delete;
	TokenService &operator=(const TokenService &) = delete;

	void runCleanThread();

	bool m_done = false;

	std::condition_variable m_cv;
	std::mutex m_cv_m;
	std::thread m_clean;

	static TokenService *s_instance;
};
