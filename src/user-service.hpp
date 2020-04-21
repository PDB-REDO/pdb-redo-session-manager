//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>

#include <zeep/serialize.hpp>

#include <pqxx/pqxx>

// --------------------------------------------------------------------

struct User
{
	unsigned long id;
	std::string name;
	std::string email;
	std::string institution;
	std::string password;

	User& operator=(const pqxx::tuple& t);

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

class UserService
{
  public:
	UserService(const UserService&) = delete;
	UserService& operator=(const UserService&) = delete;

	static void init(pqxx::connection& connection);
	static UserService& instance();

	User get_user(unsigned long id);
	User get_user(const std::string& name);

	uint32_t create_run_id(const std::string& username);

  private:
	UserService(pqxx::connection& connection);

	static std::unique_ptr<UserService> sInstance;
	pqxx::connection& m_connection;
};