//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include "prsm-config.hpp"

#include "prsm-db-connection.hpp"

// --------------------------------------------------------------------

std::unique_ptr<prsm_db_connection> prsm_db_connection::s_instance;
thread_local std::unique_ptr<pqxx::connection> prsm_db_connection::s_connection;

void prsm_db_connection::init(const std::string& connection_string)
{
	s_instance.reset(new prsm_db_connection(connection_string));
}

prsm_db_connection& prsm_db_connection::instance()
{
	return *s_instance;
}

// --------------------------------------------------------------------

prsm_db_connection::prsm_db_connection(const std::string& connectionString)
	: m_connection_string(connectionString)
{
}

pqxx::connection& prsm_db_connection::get_connection()
{
	if (not s_connection)
		s_connection.reset(new pqxx::connection(m_connection_string));
	return *s_connection;
}

void prsm_db_connection::reset()
{
	s_connection.reset();
}

// --------------------------------------------------------------------

bool prsm_db_error_handler::create_error_reply(const zeep::http::request& req, std::exception_ptr eptr, zeep::http::reply& reply)
{
	try
	{
		std::rethrow_exception(eptr);
	}
	catch (pqxx::broken_connection& ex)
	{
		std::cerr << ex.what() << std::endl;
		prsm_db_connection::instance().reset();
	}
	catch (...)
	{
	}
	
	return false;
}
