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

#include "run-service.hpp"

#include <zeep/http/login-controller.hpp>
#include <zeep/http/security.hpp>
#include <zeep/nvp.hpp>
#include <zeep/value-serializer.hpp>

#include <pqxx/pqxx>

// --------------------------------------------------------------------

struct User
{
	unsigned long id;
	std::string name;
	std::string institution;
	std::string email;
	std::string password;
	std::chrono::time_point<std::chrono::system_clock> created;

	std::optional<std::chrono::time_point<std::chrono::system_clock>> lastLogin;

	std::optional<RunStatus> lastJobStatus;
	std::optional<std::chrono::time_point<std::chrono::system_clock>> lastJobDate;
	std::optional<int> lastJobNr;

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
		   & zeep::make_nvp("password", password)
		   & zeep::make_nvp("created", created)
		   
		   & zeep::make_nvp("last-login", lastLogin)

		   & zeep::make_nvp("last-job-nr", lastJobNr)
		   & zeep::make_nvp("last-job-date", lastJobDate)
		   & zeep::make_nvp("last-job-status", lastJobStatus);
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
	bool user_is_valid(const std::string &username) const override;

	// create a new user
	uint32_t createUser(const User &user);

	void updateUser(const User &user);

	void deleteUser(int id);
	void deleteUser(const User &user)
	{
		deleteUser(user.id);
	}

	// To reset a password
	void sendNewPassword(const std::string &username, const std::string &email);

	User getUser(unsigned long id) const;
	User getUser(const std::string &name) const;

	std::vector<User> getAllUsers() const;

	uint32_t createRunID(const std::string &username);

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

	static std::unique_ptr<UserService> s_instance;
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
		const std::string &email, const std::string &password, const std::string &password2, std::optional<std::string> accept_gdpr);

	zeep::http::reply get_is_valid_password(const zeep::http::scope &scope, const std::string &password);

	zeep::http::reply get_reset_pw(const zeep::http::scope &scope);
	zeep::http::reply post_reset_pw(const zeep::http::scope &scope, const std::string &username, const std::string &email);

	zeep::http::reply get_change_pw(const zeep::http::scope &scope);
	zeep::http::reply post_change_pw(const zeep::http::scope &scope, const std::string &oldPassword, const std::string &newPassword, const std::string &newPassword2);

	zeep::http::reply get_update_info(const zeep::http::scope &scope);
	zeep::http::reply post_update_info(const zeep::http::scope &scope, const std::string &institution, const std::string &email);

	zeep::http::reply get_delete(const zeep::http::scope &scope);
	zeep::http::reply post_delete(const zeep::http::scope &scope);

	zeep::http::reply get_token_for_ccp4(const zeep::http::scope &scope, const std::string &reqid, const std::string &cburl);

	zeep::http::reply getTokens(const zeep::http::scope &scope);
	zeep::http::reply createToken(const zeep::http::scope &scope, std::string name);
	zeep::http::reply deleteToken(const zeep::http::scope &scope, unsigned long id);

	zeep::http::reply requestToken(const zeep::http::scope &scope, std::string name);
};
