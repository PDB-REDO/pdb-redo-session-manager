//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <cassert>

#include "user-service.hpp"

// --------------------------------------------------------------------

User& User::operator=(const pqxx::tuple& t)
{
	id			= t.at("id").as<unsigned long>();
	name		= t.at("name").as<std::string>();
	email		= t.at("email_address").as<std::string>();
	institution	= t.at("institution").as<std::string>();
	password	= t.at("password").as<std::string>();
	// created		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("created").as<std::string>());
	// expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>());

	return *this;
}

// --------------------------------------------------------------------

std::unique_ptr<UserService> UserService::sInstance;

// --------------------------------------------------------------------

UserService::UserService(pqxx::connection& connection)
	: m_connection(connection)
{
	m_connection.prepare("get-user-by-id",
		R"(SELECT * FROM auth_user WHERE id = $1)");
	m_connection.prepare("get-user-by-name",
		R"(SELECT * FROM auth_user WHERE name = $1)");

	m_connection.prepare("get-next-run-id",
		R"(UPDATE auth_user
			  SET last_submitted_job_id = CASE WHEN last_submitted_job_id IS NULL THEN 1 ELSE last_submitted_job_id + 1 END,
			  	  last_submitted_job_status = 1,
				  last_submitted_job_date = CURRENT_TIMESTAMP
		    WHERE name = $1
		RETURNING last_submitted_job_id)");
}

void UserService::init(pqxx::connection& connection)
{
	assert(not sInstance);
	sInstance.reset(new UserService(connection));
}

UserService& UserService::instance()
{
	assert(sInstance);
	return *sInstance;
}

User UserService::get_user(unsigned long id)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-user-by-id")(id).exec();

	if (r.empty() or r.size() != 1)
		throw std::runtime_error("Invalid user id");

	User result;
	result = r.front();

	tx.commit();

	return result;
}

User UserService::get_user(const std::string& name)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-user-by-name")(name).exec();

	if (r.empty() or r.size() != 1)
		throw std::runtime_error("Invalid user id");

	User result;
	result = r.front();

	tx.commit();

	return result;
}

uint32_t UserService::create_run_id(const std::string& username)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-next-run-id")(username).exec();

	if (r.empty() or r.size() != 1)
		throw std::runtime_error("Invalid user name?");

	auto result = r.front()[0].as<uint32_t>();

	tx.commit();

	return result;
}
