//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <mutex>

#include <pqxx/pqxx>

#include <zeep/http/error-handler.hpp>

class prsm_db_connection
{
  public:
	static void init(const std::string& connection_string);
	static prsm_db_connection& instance();

	static pqxx::work start_transaction()
	{
		return pqxx::work(instance());
	}

	pqxx::connection& get_connection();

	operator pqxx::connection&()
	{
		return get_connection();
	}

	void reset();

  private:
	prsm_db_connection(const prsm_db_connection&) = delete;
	prsm_db_connection& operator=(const prsm_db_connection&) = delete;

	prsm_db_connection(const std::string& connectionString);

	std::string m_connection_string;

	static std::unique_ptr<prsm_db_connection> s_instance;
	static thread_local std::unique_ptr<pqxx::connection> s_connection;
};

// --------------------------------------------------------------------

class prsm_db_error_handler : public zeep::http::error_handler
{
  public:

	virtual bool create_error_reply(const zeep::http::request& req, std::exception_ptr eptr, zeep::http::reply& reply);
};


