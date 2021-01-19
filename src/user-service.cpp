//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <cassert>

#include <boost/algorithm/string.hpp>

#include "user-service.hpp"
#include "prsm-db-connection.hpp"

namespace ba = boost::algorithm;

// --------------------------------------------------------------------

User::User(const pqxx::row& row)
{
	id			= row.at("id").as<unsigned long>();
	name		= row.at("name").as<std::string>();
	email		= row.at("email_address").as<std::string>();
	institution	= row.at("institution").as<std::string>();
	password	= row.at("password").as<std::string>();
}

User& User::operator=(const pqxx::row& row)
{
	id			= row.at("id").as<unsigned long>();
	name		= row.at("name").as<std::string>();
	email		= row.at("email_address").as<std::string>();
	institution	= row.at("institution").as<std::string>();
	password	= row.at("password").as<std::string>();
	// created		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("created").as<std::string>());
	// expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>());

	return *this;
}

// --------------------------------------------------------------------

const int
	kIterations = 10000,
	kSaltLength = 16,
	kKeyLength = 256;

std::string prsm_pw_encoder::encode(const std::string& password) const
{
	return {};
}

bool prsm_pw_encoder::matches(const std::string& raw_password, const std::string& stored_password) const
{
	bool result = false;

	if (stored_password[0] == '!')
	{
		std::string b = zeep::decode_base64(stored_password.substr(1));
		std::string test = zeep::pbkdf2_hmac_sha1(b.substr(0, kSaltLength), raw_password, kIterations, kKeyLength / 8);

		result = b.substr(kSaltLength) == test;
	}
	else
		result = zeep::encode_base64(zeep::md5(raw_password)) == stored_password;	

	return result;
}

// --------------------------------------------------------------------

std::unique_ptr<UserService> UserService::sInstance;

// --------------------------------------------------------------------

UserService::UserService(const std::string& admins)
{
	ba::split(m_admins, admins, ba::is_any_of(",; "));
}

void UserService::init(const std::string& admins)
{
	assert(not sInstance);
	sInstance.reset(new UserService(admins));
}

UserService& UserService::instance()
{
	assert(sInstance);
	return *sInstance;
}

User UserService::get_user(unsigned long id) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(R"(SELECT * FROM auth_user WHERE id = )" + std::to_string(id));

	tx.commit();

	return User(r);
}

User UserService::get_user(const std::string& name) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(R"(SELECT * FROM auth_user WHERE name = )" + tx.quote(name));

	tx.commit();

	return User(r);
}

uint32_t UserService::create_run_id(const std::string& username)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(UPDATE auth_user
			  SET last_submitted_job_id = CASE WHEN last_submitted_job_id IS NULL THEN 1 ELSE last_submitted_job_id + 1 END,
			  	  last_submitted_job_status = 1,
				  last_submitted_job_date = CURRENT_TIMESTAMP
		    WHERE name = )" + tx.quote(username) + R"(
		RETURNING last_submitted_job_id)");

	tx.commit();

	return r[0].as<uint32_t>();
}

zeep::http::user_details UserService::load_user(const std::string& username) const
{
	zeep::http::user_details result;

	User user = get_user(username);

	result.username = user.name;
	result.password = user.password;
	result.roles.insert("USER");
	if (std::find(m_admins.begin(), m_admins.end(), user.name) != m_admins.end())
		result.roles.insert("ADMIN");

	return result;
}
