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

#include <mailio/message.hpp>
#include <mailio/smtp.hpp>

#include <mrsrc.hpp>

#include <mcfp.hpp>

#include <cassert>
#include <iostream>
#include <random>

// --------------------------------------------------------------------

namespace
{
const double kMinimalPasswordEntropy = 50;

const std::set<std::string> kAmbiguous{ "B", "8", "G", "6", "I", "1", "l", "0", "O", "Q", "D", "S", "5", "Z", "2" };
const std::vector<std::string> kVowels{ "a", "ae", "ah", "ai", "e", "ee", "ei", "i", "ie", "o", "oh", "oo", "u" };
const std::vector<std::string> kConsonants{ "b", "c", "ch", "d", "f", "g", "gh", "h", "j", "k", "l", "m", "n", "ng", "p", "ph", "qu", "r", "s", "sh", "t", "th", "v", "w", "x", "y", "z" };
const std::vector<char> kSymbols{ '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~' };
} // namespace

// --------------------------------------------------------------------

User::User(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	email = row.at("email").as<std::string>();
	institution = row.at("institution").as<std::string>();
	password = row.at("password").as<std::string>();
}

User &User::operator=(const pqxx::row &row)
{
	id = row.at("id").as<unsigned long>();
	name = row.at("name").as<std::string>();
	email = row.at("email").as<std::string>();
	institution = row.at("institution").as<std::string>();
	password = row.at("password").as<std::string>();

	return *this;
}

// --------------------------------------------------------------------

const int
	kIterations = 10000,
	kSaltLength = 16,
	kKeyLength = 256;

std::string PasswordEncoder::encode(const std::string &password) const
{
	return {};
}

bool PasswordEncoder::matches(const std::string &raw_password, const std::string &stored_password) const
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
	auto r = tx.exec1(R"(SELECT * FROM public.user WHERE id = )" + std::to_string(id));

	tx.commit();

	return User(r);
}

User UserService::get_user(const std::string &name) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(R"(SELECT * FROM public.user WHERE name = )" + tx.quote(name));

	tx.commit();

	return User(r);
}

uint32_t UserService::create_run_id(const std::string &username)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(UPDATE public.user
			  SET last_job_nr = last_job_nr + 1,
				  last_job_date = CURRENT_TIMESTAMP
		    WHERE name = )" +
		tx.quote(username) + R"(
		RETURNING last_job_nr)");

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

uint32_t UserService::storeUser(const User &user)
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec1(
		R"(INSERT
			 INTO public.user (name, institution, email, password)
		   VALUES (
				 )" + tx.quote(user.name) + R"(,
				 )" + tx.quote(user.institution) + R"(,
				 )" + tx.quote(user.email) + R"(,
				 )" + tx.quote(user.password) + R"(
		   )
		RETURNING id)");

	tx.commit();

	return r[0].as<uint32_t>();
}

auto UserService::isValidUser(const User &user) const -> UserService::UserValidation
{
	UserValidation valid{};

	std::regex rxEmail(R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))", std::regex::icase);
	valid.validEmail = std::regex_match(user.email, rxEmail);

	std::regex rxName(R"(^[a-z0-9][-a-z0-9._]*$)", std::regex::icase);
	valid.validName = std::regex_match(user.name, rxName);

	valid.validInstitution = not user.institution.empty();
	valid.validPassword = not user.password.empty();

	return valid;
}

auto UserService::isValidNewUser(const User &user) const -> UserService::UserValidation
{
	auto valid = isValidUser(user);

	if (valid)
	{
		pqxx::transaction tx(prsm_db_connection::instance());
		auto r = tx.exec1(
			R"(SELECT COUNT(*) FROM public.user WHERE name = )" + tx.quote(user.name));

		tx.commit();

		valid.validName = r[0].as<uint32_t>() == 0;
	}

	if (valid)
	{
		pqxx::transaction tx(prsm_db_connection::instance());
		auto r = tx.exec1(
			R"(SELECT COUNT(*) FROM public.user WHERE email = )" + tx.quote(user.email));

		tx.commit();

		valid.validEmail = r[0].as<uint32_t>() == 0;
	}

	if (valid)
		valid.validPassword = isValidPassword(user.password);

	return valid;
}

bool UserService::isValidPassword(const std::string &password) const
{
	// calculate the password entropy, should be 50 or more

	bool lowerSeen = false, upperSeen = false, digitSeen = false, symbolSeen = false;

	for (auto ch : password)
	{
		if (std::islower(ch))
			lowerSeen = true;
		else if (std::isupper(ch))
			upperSeen = true;
		else if (std::isdigit(ch))
			digitSeen = true;
		else if (find(kSymbols.begin(), kSymbols.end(), ch) != kSymbols.end())
			symbolSeen = true;
		else if (std::isspace(ch))
			return false;
	}

	size_t poolSize = 0;
	if (lowerSeen)
		poolSize += 26;
	if (upperSeen)
		poolSize += 26;
	if (digitSeen)
		poolSize += 10;
	if (symbolSeen)
		poolSize += kSymbols.size();

	double entropy = std::log2(poolSize) * password.length();

	std::cerr << "entropy: " << entropy << std::endl;

	return entropy > kMinimalPasswordEntropy;
}

void UserService::sendNewPassword(const std::string &username, const std::string &email)
{
	std::cerr << "Request reset password for " << email << std::endl;

	try
	{
		User user = get_user(username);

		if (user.email != email)
			throw std::runtime_error("Username and e-mail address do not match");

		std::string newPassword = generatePassword();

		zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength / 8);
		std::string newPasswordHash = enc.encode(newPassword);

		std::cerr << "Reset password for " << email << " to " << newPasswordHash << std::endl;

		// --------------------------------------------------------------------

		pqxx::transaction tx(prsm_db_connection::instance());
		auto r = tx.exec0(
			R"(UPDATE public.user
				SET password = )" +
			tx.quote(newPasswordHash) + R"(,
					updated = CURRENT_TIMESTAMP
				WHERE name = )" +
			tx.quote(username));

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

		std::string smtp_user, smtp_password;

		if (config.has("smtp-user"))
			smtp_user = config.get("smtp-user");

		if (config.has("smtp-password"))
			smtp_password = config.get("smtp-password");

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

	bool vowel = rng();
	bool wasVowel = false, hasDigits = false, hasSymbols = false, hasCapitals = false;

	for (;;)
	{
		if (result.length() >= length)
		{
			if (result.length() > length or
				includeDigits != hasDigits or
				includeSymbols != hasSymbols or
				includeCapitals != hasCapitals)
			{
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
				s = kVowels[rng() % kVowels.size()];
			while (wasVowel and s.length() > 1);
		}
		else
			s = kConsonants[rng() % kConsonants.size()];

		if (s.length() + result.length() > length)
			continue;

		if (noAmbiguous and kAmbiguous.count(s))
			continue;

		if (includeCapitals and (result.length() == s.length() or vowel == false) and (rng() % 10) < 2)
		{
			for (auto &ch : s)
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
			do
				ch = (rng() % 10) + '0';
			while (noAmbiguous and kAmbiguous.count(ch));

			result += ch;
			hasDigits = true;
		}
		else if (hasSymbols == false and includeSymbols and (rng() % 10) < 2)
		{

			result += kSymbols[rng() % kSymbols.size()];
			hasSymbols = true;
		}
	}

	return result;
}

// --------------------------------------------------------------------

UserHTMLController::UserHTMLController()
	: zeep::http::login_controller()
{
	map_get("register", &UserHTMLController::get_register);
	map_post("register", &UserHTMLController::post_register, "username", "institution", "email", "password");

	map_get("reset-password", &UserHTMLController::get_reset_pw);
	map_post("reset-password", &UserHTMLController::post_reset_pw, "username", "email");
}

zeep::xml::document UserHTMLController::load_login_form(const zeep::http::request &req) const
{
	std::string uri = get_prefixless_path(req);

	zeep::http::scope scope(m_server, req);

	scope.put("baseuri", uri);
	scope.put("dialog", "login");

	auto &tp = m_server->get_template_processor();

	zeep::xml::document doc;
	doc.set_preserve_cdata(true);

	tp.load_template("index", doc);
	tp.process_tags(doc.child(), scope);

	return doc;
}

zeep::http::reply UserHTMLController::get_register(const zeep::http::scope &scope)
{
	zeep::http::scope sub(scope);
	sub.put("dialog", "register");
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::post_register(const zeep::http::scope &scope, const std::string &username, const std::string &institution,
	const std::string &email, const std::string &password)
{
	UserService &userService = UserService::instance();

	User user(username, institution, email, password);

	auto valid = userService.isValidNewUser(user);

	if (not valid)
	{
		auto &req = scope.get_request();
		zeep::http::scope sub(scope);
		std::string uri = get_prefixless_path(req);

		zeep::http::scope scope(m_server, req);

		scope.put("baseuri", uri);
		scope.put("dialog", "register");

		auto &tp = m_server->get_template_processor();

		zeep::xml::document doc;
		doc.set_preserve_cdata(true);

		tp.load_template("index", doc);
		tp.process_tags(doc.child(), scope);

		for (auto csrf_attr : doc.find("//input[@name='_csrf']"))
			csrf_attr->set_attribute("value", req.get_cookie("csrf-token"));

		auto user_field = doc.find_first("//input[@name='username']");
		user_field->set_attribute("value", username);
		if (not valid.validName)
			user_field->set_attribute("class", user_field->get_attribute("class") + " is-invalid");

		auto institution_field = doc.find_first("//input[@name='institution']");
		institution_field->set_attribute("value", institution);
		if (not valid.validInstitution)
			institution_field->set_attribute("class", institution_field->get_attribute("class") + " is-invalid");

		auto email_field = doc.find_first("//input[@name='email']");
		email_field->set_attribute("value", email);
		if (not valid.validEmail)
			email_field->set_attribute("class", email_field->get_attribute("class") + " is-invalid");

		auto password_field = doc.find_first("//input[@name='password']");
		// password->set_attribute("value", user.password);
		if (not valid.validPassword)
			password_field->set_attribute("class", password_field->get_attribute("class") + " is-invalid");

		for (auto i_uri : doc.find("//input[@name='uri']"))
			i_uri->set_attribute("value", uri);

		auto rep = zeep::http::reply::stock_reply(zeep::http::ok);
		rep.set_content(doc);
		return rep;
	}

	zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength / 8);
	user.password = enc.encode(password);

	UserService::instance().storeUser(user);

	return create_redirect_for_request(scope.get_request());
}

zeep::http::reply UserHTMLController::get_reset_pw(const zeep::http::scope &scope)
{
	zeep::http::scope sub(scope);
	sub.put("dialog", "reset");
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::post_reset_pw(const zeep::http::scope &scope, const std::string &username, const std::string &email)
{
	UserService::instance().sendNewPassword(username, email);

	return create_redirect_for_request(scope.get_request());
}
