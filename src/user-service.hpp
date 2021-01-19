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

#pragma once

#include <string>

#include <zeep/nvp.hpp>
#include <zeep/http/security.hpp>

#include <pqxx/pqxx>

// --------------------------------------------------------------------

struct User
{
	unsigned long id;
	std::string name;
	std::string email;
	std::string institution;
	std::string password;

	User(const pqxx::row& row);
	User& operator=(const pqxx::row& row);

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("email", email)
		   & zeep::make_nvp("institution", institution)
		   & zeep::make_nvp("password", password);
	}
};

// --------------------------------------------------------------------

class prsm_pw_encoder : public zeep::http::password_encoder
{
  public:
	static constexpr const char* name()			{ return ""; };

	virtual std::string encode(const std::string& password) const;
	virtual bool matches(const std::string& raw_password, const std::string& stored_password) const;
};

// --------------------------------------------------------------------

class UserService : public zeep::http::user_service
{
  public:
	UserService(const UserService&) = delete;
	UserService& operator=(const UserService&) = delete;

	static void init(const std::string& admins);
	static UserService& instance();

	/// Validate the authorization, returns the validated user. Throws unauthorized_exception in case of failure
	virtual zeep::http::user_details load_user(const std::string& username) const;

	// create a password hash
	static std::string create_password_hash(const std::string& password);

	User get_user(unsigned long id) const;
	User get_user(const std::string& name) const;

	uint32_t create_run_id(const std::string& username);

  private:

	UserService(const std::string& admins);

	static std::unique_ptr<UserService> sInstance;
	std::vector<std::string> m_admins;
};