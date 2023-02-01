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

#include <zeep/http/login-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>

#include <pqxx/pqxx>

// --------------------------------------------------------------------

struct User
{
	unsigned long id;
	std::string name;
	std::string institution;
	std::string email;
	std::string password;

	User(const std::string &name, const std::string &institution, const std::string &email, const std::string &password)
		: name(name)
		, institution(institution)
		, email(email)
		, password(password)
	{
	}

	User(const pqxx::row &row);
	User &operator=(const pqxx::row &row);

	template <typename Archive>
	void serialize(Archive &ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("institution", institution)
		   & zeep::make_nvp("email", email)
		   & zeep::make_nvp("password", password);
	}
};

// --------------------------------------------------------------------

class PasswordEncoder : public zeep::http::password_encoder
{
  public:
	static constexpr const char *name() { return ""; };

	virtual std::string encode(const std::string &password) const;
	virtual bool matches(const std::string &raw_password, const std::string &stored_password) const;
};

// --------------------------------------------------------------------

class UserService : public zeep::http::user_service
{
  public:
	UserService(const UserService &) = delete;
	UserService &operator=(const UserService &) = delete;

	static void init(const std::string &admins);
	static UserService &instance();

	/// Validate the authorization, returns the validated user. Throws unauthorized_exception in case of failure
	zeep::http::user_details load_user(const std::string &username) const override;

	// create a password hash
	static std::string create_password_hash(const std::string &password);

	// create a new user
	uint32_t createUser(const User &user);

	void updateUser(const User &user);

	// To reset a password
	void sendNewPassword(const std::string &username, const std::string &email);

	User get_user(unsigned long id) const;
	User get_user(const std::string &name) const;

	uint32_t create_run_id(const std::string &username);

	struct UserValidation
	{
		bool validName, validEmail, validInstitution, validPassword;

		explicit operator bool() { return validName and validEmail and validInstitution and validPassword; }
	};

	UserValidation isValidUser(const User &user) const;
	UserValidation isValidNewUser(const User &user) const;

	bool isValidEmailForUser(const User &user, const std::string &email);

  private:
	UserService(const std::string &admins);

	std::string generatePassword() const;

	static std::unique_ptr<UserService> sInstance;
	std::vector<std::string> m_admins;
};

// --------------------------------------------------------------------

class UserHTMLController : public zeep::http::login_controller
{
  public:
	UserHTMLController();

	zeep::xml::document load_login_form(const zeep::http::request &req) const override;

	zeep::http::reply get_register(const zeep::http::scope &scope);
	zeep::http::reply post_register(const zeep::http::scope &scope, const std::string &username, const std::string &institution,
		const std::string &email, const std::string &password, const std::string &password2);

	zeep::http::reply get_reset_pw(const zeep::http::scope &scope);
	zeep::http::reply post_reset_pw(const zeep::http::scope &scope, const std::string &username, const std::string &email);

	zeep::http::reply get_change_pw(const zeep::http::scope &scope);
	zeep::http::reply post_change_pw(const zeep::http::scope &scope, const std::string &oldPassword, const std::string &newPassword, const std::string &newPassword2);

	zeep::http::reply get_update_info(const zeep::http::scope &scope);
	zeep::http::reply post_update_info(const zeep::http::scope &scope, const std::string &institution, const std::string &email);
};