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

#include "user-service.hpp"
#include "prsm-db-connection.hpp"

#include <mailio/smtp.hpp>
#include <mailio/message.hpp>

#include <mrsrc.hpp>

#include <mcfp.hpp>

#include <cassert>
#include <iostream>
#include <random>

// --------------------------------------------------------------------

User::User(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	email = row.at("email_address").as<std::string>();
	institution = row.at("institution").as<std::string>();
	password = row.at("password").as<std::string>();
}

User &User::operator=(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	email = row.at("email_address").as<std::string>();
	institution = row.at("institution").as<std::string>();
	password = row.at("password").as<std::string>();

	return *this;
}

// --------------------------------------------------------------------

const int
	kIterations = 10000,
	kSaltLength = 16,
	kKeyLength = 256;

std::string prsm_pw_encoder::encode(const std::string &password) const
{
	return {};
}

bool prsm_pw_encoder::matches(const std::string &raw_password, const std::string &stored_password) const
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

UserService::UserService(const std::string &admins)
{
	for (std::string::size_type i = 0, j = admins.find_first_of(",; ");;)
	{
		m_admins.push_back(admins.substr(i, j - i));
		if (j == std::string::npos)
			break;
		i = j + 1;
		j = admins.find_first_of(",; ", i);
	}
}

void UserService::init(const std::string &admins)
{
	assert(not sInstance);
	sInstance.reset(new UserService(admins));
}

UserService &UserService::instance()
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

User UserService::get_user(const std::string &name) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(R"(SELECT * FROM auth_user WHERE name = )" + tx.quote(name));

	tx.commit();

	return User(r);
}

uint32_t UserService::create_run_id(const std::string &username)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(UPDATE auth_user
			  SET last_submitted_job_id = CASE WHEN last_submitted_job_id IS NULL THEN 1 ELSE last_submitted_job_id + 1 END,
			  	  last_submitted_job_status = 1,
				  last_submitted_job_date = CURRENT_TIMESTAMP
		    WHERE name = )" +
		tx.quote(username) + R"(
		RETURNING last_submitted_job_id)");

	tx.commit();

	return r[0].as<uint32_t>();
}

zeep::http::user_details UserService::load_user(const std::string &username) const
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

bool UserService::isValidEmail(const std::string &email) const
{
	std::regex rx(R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))", std::regex::icase);
	return std::regex_match(email, rx);
}

void UserService::sendNewPassword(const std::string &username, const std::string &email)
{
	std::cerr << "Request reset password for " << email << std::endl;

	try
	{
		if (not isValidEmail(email))
			throw std::runtime_error("Not a valid e-mail address");

		User user = get_user(username);

		if (user.email != email)
			throw std::runtime_error("Username and e-mail address do not match");

		std::string newPassword = generatePassword();

		zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength);
		std::string newPasswordHash = enc.encode(newPassword);

		std::cerr << "Reset password for " << email << " to " << newPasswordHash << std::endl;

		// --------------------------------------------------------------------
		
		pqxx::transaction tx(prsm_db_connection::instance());
		auto r = tx.exec1(
			R"(UPDATE auth_user
				SET password = )" + tx.quote(newPasswordHash) + R"(
				WHERE name = )" +
			tx.quote(username) + R"(
			RETURNING last_submitted_job_id)");

		// --------------------------------------------------------------------

		mailio::message msg;

		msg.add_from(mailio::mail_address("PDB-REDO User Management Service", "pdb-redo@nki.nl"));
		msg.add_recipient(mailio::mail_address("PDB-REDO user", email));
		msg.subject("New password for PDB-REDO");

		std::ostringstream content;

		mrsrc::istream is("reset-password-mail.txt");

		std::string line;
		while (std::getline(is, line))
		{
			auto i = line.find("^1");
			if (i != std::string::npos)
				line.replace(i, 2, newPassword);
			content << line << std::endl;
		}

		msg.content(content.str());
		msg.content_type(mailio::mime::media_type_t::TEXT, "plain", "utf-8");
		msg.content_transfer_encoding(mailio::mime::content_transfer_encoding_t::BINARY);

		// Fetch the smtp info
		auto &config = mcfp::config::instance();

		auto smtp_user = config.get("smtp-user");
		auto smtp_password = config.get("smtp-password");
		auto smtp_host = config.get("smtp-host");
		auto smtp_port = config.get<uint16_t>("smtp-port");

		if (smtp_port == 25)
		{
			mailio::smtp conn(smtp_host, smtp_port);
			conn.authenticate(smtp_user, smtp_password, smtp_user.empty() ? mailio::smtp::auth_method_t::NONE : mailio::smtp::auth_method_t::LOGIN);
			conn.submit(msg);	
		}
		else if (smtp_port == 465)
		{
			mailio::smtps conn(smtp_host, smtp_port);
			conn.authenticate(smtp_user, smtp_password, smtp_user.empty() ? mailio::smtps::auth_method_t::NONE : mailio::smtps::auth_method_t::LOGIN);
			conn.submit(msg);	
		}
		else if (smtp_port == 587 and not smtp_user.empty())
		{
			mailio::smtps conn(smtp_host, smtp_port);
			conn.authenticate(smtp_user, smtp_password, mailio::smtps::auth_method_t::START_TLS);
			conn.submit(msg);	
		}
		else
			throw std::runtime_error("Unable to send message, smtp configuration error");

		// --------------------------------------------------------------------
		// Sending the new password succeeded

		tx.commit();
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Sending new password failed: " << ex.what() << std::endl;
	}
}

std::string UserService::generatePassword() const
{
	const bool
		includeDigits = true,
		includeSymbols = true,
		includeCapitals = true,
		noAmbiguous = true;
	const int length = 10;

	std::random_device rng;

	std::string result;

	std::set<std::string> kAmbiguous{ "B", "8", "G", "6", "I", "1", "l", "0", "O", "Q", "D", "S", "5", "Z", "2" };

	std::vector<std::string> vowels{ "a", "ae", "ah", "ai", "e", "ee", "ei", "i", "ie", "o", "oh", "oo", "u" };
	std::vector<std::string> consonants{ "b", "c", "ch", "d", "f", "g", "gh", "h", "j", "k", "l", "m", "n", "ng", "p", "ph", "qu", "r", "s", "sh", "t", "th", "v", "w", "x", "y", "z" };

	bool vowel = rng();
	bool wasVowel = false, hasDigits = false, hasSymbols = false, hasCapitals = false;

	for (;;)
	{
		if (result.length() >= length)
		{
			if (result.length() > length or
					includeDigits != hasDigits or
					includeSymbols != hasSymbols or
					includeCapitals != hasCapitals) {
				result.clear();
				hasDigits = hasSymbols = hasCapitals = false;
				continue;
			}

			break;
		}

		std::string s;
		if (vowel)
		{
			do
				s = vowels[rng() % vowels.size()];
			while (wasVowel and s.length() > 1);
		}
		else
			s = consonants[rng() % consonants.size()];

		if (s.length() + result.length() > length)
			continue;

		if (noAmbiguous and kAmbiguous.count(s))
			continue;

		if (includeCapitals and (result.length() == s.length() or vowel == false) and (rng() % 10) < 2)
		{
			for (auto& ch: s)
				ch = std::toupper(ch);
			hasCapitals = true;
		}
		result += s;

		if (vowel and (wasVowel or s.length() > 1 or (rng() % 10) > 3))
		{
			vowel = false;
			wasVowel = true;
		}
		else
		{
			wasVowel = vowel;
			vowel = true;
		}

		if (hasDigits == false and includeDigits and (rng() % 10) < 3)
		{
			std::string ch;
			do ch = (rng() % 10) + '0';
			while (noAmbiguous and kAmbiguous.count(ch));

			result += ch;
			hasDigits = true;
		}
		else if (hasSymbols == false and includeSymbols and (rng() % 10) < 2)
		{
			std::vector<char> kSymbols
					{
							'!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+',
							',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@',
							'[', '\\', ']', '^', '_', '`', '{', '|', '}', '~',
					};

			result += kSymbols[rng() % kSymbols.size()];
			hasSymbols = true;
		}
	}

	return result;
}