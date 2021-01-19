//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

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