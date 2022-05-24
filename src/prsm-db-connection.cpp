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
