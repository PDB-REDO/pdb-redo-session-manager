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

#include "mrsrc.hpp"
#include "prsm-db-connection.hpp"
#include "run-service.hpp"
#include "token-service.hpp"

#include <algorithm>
#include <boost/asio/deadline_timer.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mailio/message.hpp>
#include <mailio/smtp.hpp>
#include <mcfp/mcfp.hpp>
#include <random>
#include <stdexcept>
#include <zeep/uri.hpp>

// --------------------------------------------------------------------

namespace
{
const double kMinimalPasswordEntropy = 50;

// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
const std::set<std::string> kAmbiguous{ "B", "8", "G", "6", "I", "1", "l", "0", "O", "Q", "D", "S", "5", "Z", "2" };
const std::vector<std::string> kVowels{ "a", "ae", "ah", "ai", "e", "ee", "ei", "i", "ie", "o", "oh", "oo", "u" };
const std::vector<std::string> kConsonants{ "b", "c", "ch", "d", "f", "g", "gh", "h", "j", "k", "l", "m", "n", "ng", "p", "ph", "qu", "r", "s", "sh", "t", "th", "v", "w", "x", "y", "z" };
const std::vector<char> kSymbols{ '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~' };

std::regex kEmailRX(R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\]))", std::regex::icase);
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)
} // namespace

// --------------------------------------------------------------------

User::User(const pqxx::row &row)
{
	id = row.at("id").get<uint64_t>().value_or(0);
	name = row.at("name").get<std::string>().value_or("");
	email = row.at("email").get<std::string>().value_or("");
	institution = row.at("institution").get<std::string>().value_or("");
	password = row.at("password").get<std::string>().value_or("");
	created = parse_timestamp(row.at("created").get<std::string>().value_or(""));

	if (auto v = row.at("last_login").get<std::string>(); v)
		lastLogin = parse_timestamp(*v);

	if (auto v = row.at("last_job_date").get<std::string>(); v)
		lastJobDate = parse_timestamp(*v);
	lastJobNr = row.at("last_job_nr").get<int>();
	if (auto v = row.at("last_job_status").get<std::string>(); v)
	{
		try
		{
			lastJobStatus = zeep::value_serializer<RunStatus>::from_string(*v);
		}
		catch (const std::invalid_argument &ex)
		{
			auto s = *v;
			int vi;
			auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.length(), vi);
			if (ptr == s.data() + s.length() and ec == std::errc{} and vi >= 0 and vi < static_cast<int>(RunStatus::DELETING))
				lastJobStatus = static_cast<RunStatus>(vi);
		}
	}
}


bool User::shouldRenewPassword() const
{
	bool result = true;

	auto parts = zeep::split(password, "$");
	
	if (parts.size() == 4 and parts.front() == "pbkdf2_sha256")
	{
		int iterations;
		const auto &[ptr, ec] = std::from_chars(parts[1].data(), parts[1].data() + parts[1].size(), iterations);

		if (ec == std::errc{} and ptr == parts[1].data() + parts[1].length() and iterations >= 100'000)
			result = false;
	}

	return result;
}

// --------------------------------------------------------------------

const int
	kIterations = 10000,
	kSaltLength = 16,
	kKeyLength = 256;

std::string PasswordEncoder::encode(const std::string & /*password*/) const
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

	return result;
}

// --------------------------------------------------------------------

bool isValidPassword(const std::string &password)
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
		else if (std::ranges::find(kSymbols, ch) != kSymbols.end())
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

	return entropy > kMinimalPasswordEntropy;
}

// --------------------------------------------------------------------

std::unique_ptr<UserService> UserService::s_instance;

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
	assert(not s_instance);
	s_instance.reset(new UserService(admins));
}

UserService &UserService::instance()
{
	assert(s_instance);
	return *s_instance;
}

User UserService::getUser(uint64_t id) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec(R"(SELECT * FROM redo.user WHERE id = )" + std::to_string(id)).one_row();

	tx.commit();

	return r;
}

User UserService::getUser(const std::string &name) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	auto r = tx.exec(R"(SELECT * FROM redo.user WHERE name = )" + tx.quote(name)).one_row();

	tx.commit();

	return r;
}

std::vector<User> UserService::getAllUsers() const
{
	std::vector<User> result;

	pqxx::transaction tx(prsm_db_connection::instance());
	auto rows = tx.exec(R"(SELECT * FROM redo.user ORDER BY created DESC)");

	for (auto row : rows)
		result.emplace_back(row);

	tx.commit();

	return result;
}

uint32_t UserService::createRunID(const std::string &username)
{
	pqxx::work tx(prsm_db_connection::instance());

	auto result = tx.query_value<uint32_t>(
		R"(UPDATE redo.user
			  SET last_job_nr = last_job_nr + 1,
				  last_job_date = CURRENT_TIMESTAMP
		    WHERE name = )" +
		tx.quote(username) + R"(
		RETURNING last_job_nr)");
	tx.commit();
	return result;
}

zeep::http::user_details UserService::load_user(const std::string &username) const
{
	zeep::http::user_details result;

	try
	{
		pqxx::transaction tx_1(prsm_db_connection::instance());
		auto r = tx_1.exec(R"(SELECT * FROM redo.user WHERE name = )" + tx_1.quote(username)).one_row();
		tx_1.commit();

		User user(r);

		pqxx::transaction tx_2(prsm_db_connection::instance());
		tx_2.exec(R"(UPDATE redo.user SET last_login = CURRENT_TIMESTAMP WHERE id = )" + tx_2.quote(user.id)).no_rows();
		tx_2.commit();

		result.username = user.name;
		result.password = user.password;
		result.roles.insert("USER");
		if (std::ranges::find(m_admins, user.name) != m_admins.end())
			result.roles.insert("ADMIN");
	}
	catch (...)
	{
		result = {};
	}

	return result;
}

bool UserService::user_is_valid(const std::string &username) const
{
	pqxx::transaction tx(prsm_db_connection::instance());
	return tx.query_value<int>(R"(SELECT COUNT(*) FROM redo.user WHERE name = )" + tx.quote(username)) == 1;
}

uint32_t UserService::createUser(const User &user)
{
	pqxx::work tx(prsm_db_connection::instance());

	auto result = tx.query_value<uint32_t>(
		// clang-format off
		R"(INSERT
			 INTO redo.user (name, institution, email, password)
		   VALUES ()"
		   	+ tx.quote(user.name) + ","
		   	+ tx.quote(user.institution) + ","
		   	+ tx.quote(user.email) + ","
			+ tx.quote(user.password) + R"()
		RETURNING id)");
		//clang-format on
	tx.commit();
	return result;
}

void UserService::updateUser(const User &user)
{
	pqxx::transaction tx(prsm_db_connection::instance());

	std::vector<std::string> set;

	if (not user.name.empty())
		set.push_back("name = " + tx.quote(user.name));

	if (not user.institution.empty())
		set.push_back("institution = " + tx.quote(user.institution));

	if (not user.email.empty())
		set.push_back("email = " + tx.quote(user.email));

	if (not user.password.empty())
		set.push_back("password = " + tx.quote(user.password));

	if (not set.empty())
	{
		tx.exec("UPDATE redo.user SET " + zeep::join(set, ", ") + " WHERE id = " + tx.quote(user.id)).no_rows();
		tx.commit();
	}
}

void UserService::deleteUser(uint64_t id)
{
	User user = getUser(id);

	pqxx::transaction tx(prsm_db_connection::instance());

	tx.exec("DELETE FROM redo.user WHERE id = " + tx.quote(id)).no_rows();
	tx.commit();

	auto userDir = RunService::instance().getRunsDir() / user.name;
	std::error_code ec;
	std::filesystem::create_directories(userDir, ec);

	if (std::filesystem::exists(userDir, ec))
		std::ofstream touched(userDir / "deleted.txt");
}

auto UserService::isValidUser(const User &user) const -> UserService::UserValidation
{
	UserValidation valid{};

	valid.validEmail = std::regex_match(user.email, kEmailRX);

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
		valid.validName = tx.query_value<int>(
			R"(SELECT COUNT(*) FROM redo.user WHERE name = )" + tx.quote(user.name)) == 0;
	}

	if (valid)
	{
		pqxx::transaction tx(prsm_db_connection::instance());
		valid.validEmail = tx.query_value<int>(
			R"(SELECT COUNT(*) FROM redo.user WHERE email = )" + tx.quote(user.email)) == 0;
	}

// #ifndef NDEBUG
// 	if (valid and user.name == "scott" and user.password == "tiger")
// 		return valid;
// #endif

	if (valid)
		valid.validPassword = isValidPassword(user.password);

	return valid;
}

bool UserService::isValidEmailForUser(const User &user, const std::string &email)
{
	bool result = user.email == email;

	if (not result)
	{
		result = std::regex_match(email, kEmailRX);

		if (result)
		{
			pqxx::transaction tx(prsm_db_connection::instance());
			result = tx.query_value<int>(
				R"(SELECT COUNT(*) FROM redo.user WHERE email = )" + tx.quote(email) + " AND id <> " + tx.quote(user.id)) == 0;
		}
	}

	return result;
}

void UserService::sendNewPassword(const std::string &username, const std::string &email)
{
	std::cerr << "Request reset password for " << email << '\n';

	try
	{
		User user = getUser(username);

		if (user.email != email)
			throw std::runtime_error("Username and e-mail address do not match");

		std::string newPassword = generatePassword();

		zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength / 8);
		std::string newPasswordHash = enc.encode(newPassword);

		std::cerr << "Reset password for " << email << " to " << newPasswordHash << '\n';

		// --------------------------------------------------------------------

		pqxx::transaction tx(prsm_db_connection::instance());
		tx.exec(
			R"(UPDATE redo.user
				SET password = )" +
			tx.quote(newPasswordHash) + R"(
				WHERE name = )" +
			tx.quote(username))
			.no_rows();

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
			content << line << '\n';
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

		mailio::smtp conn(smtp_host, smtp_port);
		conn.authenticate(smtp_user, smtp_password,
			smtp_user.empty() ? mailio::smtp::auth_method_t::NONE : mailio::smtp::auth_method_t::LOGIN);
		conn.submit(msg);

		// --------------------------------------------------------------------
		// Sending the new password succeeded

		tx.commit();
	}
	catch (const std::exception &ex)
	{
		std::cerr << "Sending new password failed: " << ex.what() << '\n';
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
	map_post("register", &UserHTMLController::post_register, "username", "institution", "email", "password", "password-2", "accept-gdpr");

	map_get("reset-password", &UserHTMLController::get_reset_pw);
	map_post("reset-password", &UserHTMLController::post_reset_pw, "username", "email");

	map_get("change-password", &UserHTMLController::get_change_pw);
	map_post("change-password", &UserHTMLController::post_change_pw, "old-password", "new-password", "new-password-2");

	map_get("update-info", &UserHTMLController::get_update_info);
	map_post("update-info", &UserHTMLController::post_update_info, "institution", "email");

	map_get("delete", &UserHTMLController::get_delete);
	map_post("delete", &UserHTMLController::post_delete);

	map_get("ccp4-token-request", &UserHTMLController::get_token_for_ccp4, "reqid", "cburl");

	map_get("token", &UserHTMLController::getTokens);
	map_delete("token", &UserHTMLController::deleteToken, "id");
	map_post("token", &UserHTMLController::createToken, "name");

	map_post("token-request", &UserHTMLController::requestToken, "name");
}

zeem::document UserHTMLController::load_login_form(const zeep::http::request &req) const
{
	auto uri = get_prefixless_path(req);

	zeep::http::scope scope(m_server, req);

	scope.put("baseuri", uri.string());
	scope.put("dialog", "login");

	auto &tp = m_server->get_template_processor();

	zeem::document doc;
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
	const std::string &email, const std::string &password, const std::string &password2, const std::optional<std::string> &accept_gdpr)
{
	UserService &userService = UserService::instance();

	User user(username, institution, email, password);

	auto valid = userService.isValidNewUser(user);

	if (not valid or (password != password2) or not accept_gdpr.has_value())
	{
		auto &req = scope.get_request();
		zeep::http::scope sub(scope);
		auto uri = get_prefixless_path(req);

		sub.put("baseuri", uri.string());
		sub.put("dialog", "register");

		auto &tp = m_server->get_template_processor();

		zeem::document doc;
		doc.set_preserve_cdata(true);

		tp.load_template("index", doc);
		tp.process_tags(doc.child(), sub);

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

		auto password_2_field = doc.find_first("//input[@name='password-2']");
		// password->set_attribute("value", user.password);
		if (password != password2)
			password_2_field->set_attribute("class", password_2_field->get_attribute("class") + " is-invalid");

		auto gdpr_field = doc.find_first("//input[@name='accept-gdpr']");
		if (not accept_gdpr.has_value())
			gdpr_field->set_attribute("class", gdpr_field->get_attribute("class") + " is-invalid");
		else
			gdpr_field->set_attribute("checked", "checked");

		for (auto i_uri : doc.find("//input[@name='uri']"))
			i_uri->set_attribute("value", uri.string());

		auto rep = zeep::http::reply::stock_reply(zeep::http::status_type::ok);
		rep.set_content(doc);
		return rep;
	}

	zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength / 8);
	user.password = enc.encode(password);

	UserService::instance().createUser(user);

	auto reply = create_redirect_for_request(scope.get_request());

	m_server->get_security_context().add_authorization_headers(reply, userService.load_user(user.name));

	return reply;
}

zeep::http::reply UserHTMLController::get_is_valid_password(const zeep::http::scope & /*scope*/, const std::string &password)
{
	zeep::http::reply rep = zeep::http::reply::stock_reply(zeep::http::status_type::ok);
	zeep::el::object e = isValidPassword(password);
	rep.set_content(e);
	return rep;
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

zeep::http::reply UserHTMLController::get_change_pw(const zeep::http::scope &scope)
{
	zeep::http::scope sub(scope);
	sub.put("dialog", "change");
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::post_change_pw(const zeep::http::scope &scope,
	const std::string &oldPassword, const std::string &newPassword, const std::string &newPassword2)
{
	UserService &userService = UserService::instance();

	bool oldPWValid = false, newPWValid = isValidPassword(newPassword), newPW2Valid = newPassword == newPassword2;

	User user = userService.getUser(scope.get_credentials()["username"].get<std::string>());
	oldPWValid = m_server->get_security_context().verify_username_password(user.name, oldPassword);

	if (oldPWValid and newPWValid and newPW2Valid)
	{
		zeep::http::pbkdf2_sha256_password_encoder enc(kIterations, kKeyLength / 8);

		user.name.clear();
		user.institution.clear();
		user.email.clear();
		user.password = enc.encode(newPassword);

		userService.updateUser(user);

		return create_redirect_for_request(scope.get_request());
	}

	auto &req = scope.get_request();
	zeep::http::scope sub(scope);
	auto uri = get_prefixless_path(req);

	sub.put("baseuri", uri.string());
	sub.put("dialog", "change");

	auto &tp = m_server->get_template_processor();

	zeem::document doc;
	doc.set_preserve_cdata(true);

	tp.load_template("index", doc);
	tp.process_tags(doc.child(), sub);

	for (auto csrf_attr : doc.find("//input[@name='_csrf']"))
		csrf_attr->set_attribute("value", req.get_cookie("csrf-token"));

	auto old_pw_field = doc.find_first("//input[@name='old-password']");
	if (not oldPWValid)
		old_pw_field->set_attribute("class", old_pw_field->get_attribute("class") + " is-invalid");

	auto new_pw_field = doc.find_first("//input[@name='new-password']");
	if (not newPWValid)
		new_pw_field->set_attribute("class", new_pw_field->get_attribute("class") + " is-invalid");

	auto new_pw_2_field = doc.find_first("//input[@name='new-password-2']");
	if (not newPW2Valid)
		new_pw_2_field->set_attribute("class", new_pw_2_field->get_attribute("class") + " is-invalid");

	for (auto i_uri : doc.find("//input[@name='uri']"))
		i_uri->set_attribute("value", uri.string());

	auto rep = zeep::http::reply::stock_reply(zeep::http::status_type::internal_server_error);
	rep.set_content(doc);
	return rep;
}

zeep::http::reply UserHTMLController::get_update_info(const zeep::http::scope &scope)
{
	UserService &userService = UserService::instance();
	User user = userService.getUser(scope.get_credentials()["username"].get<std::string>());

	zeep::http::scope sub(scope);
	sub.put("dialog", "update");
	sub.put("institution", user.institution);
	sub.put("email", user.email);
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::post_update_info(const zeep::http::scope &scope, const std::string &institution, const std::string &email)
{
	UserService &userService = UserService::instance();
	User user = userService.getUser(scope.get_credentials()["username"].get<std::string>());

	bool institutionValid = not institution.empty(), emailValid = userService.isValidEmailForUser(user, email);

	if (institutionValid and emailValid)
	{
		user.name.clear();
		user.institution = institution;
		user.email = email;
		user.password.clear();

		userService.updateUser(user);

		return create_redirect_for_request(scope.get_request());
	}

	auto &req = scope.get_request();
	zeep::http::scope sub(scope);
	auto uri = get_prefixless_path(req);

	sub.put("baseuri", uri.string());
	sub.put("dialog", "update");

	auto &tp = m_server->get_template_processor();

	zeem::document doc;
	doc.set_preserve_cdata(true);

	tp.load_template("index", doc);
	tp.process_tags(doc.child(), sub);

	for (auto csrf_attr : doc.find("//input[@name='_csrf']"))
		csrf_attr->set_attribute("value", req.get_cookie("csrf-token"));

	auto instFld = doc.find_first("//input[@name='institution']");
	instFld->set_attribute("value", institution);
	if (not institutionValid)
		instFld->set_attribute("class", instFld->get_attribute("class") + " is-invalid");

	auto emailFld = doc.find_first("//input[@name='email']");
	emailFld->set_attribute("value", email);
	if (not emailValid)
		emailFld->set_attribute("class", emailFld->get_attribute("class") + " is-invalid");

	for (auto i_uri : doc.find("//input[@name='uri']"))
		i_uri->set_attribute("value", uri.string());

	auto rep = zeep::http::reply::stock_reply(zeep::http::status_type::internal_server_error);
	rep.set_content(doc);
	return rep;
}

zeep::http::reply UserHTMLController::get_delete(const zeep::http::scope &scope)
{
	zeep::http::scope sub(scope);
	sub.put("dialog", "delete");
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::post_delete(const zeep::http::scope &scope)
{
	UserService &userService = UserService::instance();
	User user = userService.getUser(scope.get_credentials()["username"].get<std::string>());

	userService.deleteUser(user);

	auto reply = create_redirect_for_request(scope.get_request());
	reply.set_delete_cookie("access_token");
	return reply;
}

zeep::http::reply UserHTMLController::get_token_for_ccp4(const zeep::http::scope &scope, const std::string &reqid, const std::string &cburl)
{
	if (not zeep::is_fully_qualified_uri(cburl))
		throw std::runtime_error("The callback is not a valid URI");

	zeep::http::scope sub(scope);
	sub.put("dialog", "ccp4-token-request");
	sub.put("reqid", reqid);
	sub.put("cburl", cburl);
	return get_template_processor().create_reply_from_template("index", sub);
}

zeep::http::reply UserHTMLController::getTokens(const zeep::http::scope &scope)
{
	auto username = scope.get_credentials()["username"].get<std::string>();

	zeep::http::scope sub(scope);

	auto s = TokenService::instance().getAllTokensForUser(username);
	sub.put("tokens", zeep::el::serializer<decltype(s)>::serialize(s));

	return get_template_processor().create_reply_from_template("tokens", sub);
}

zeep::http::reply UserHTMLController::deleteToken(const zeep::http::scope &scope, uint64_t id)
{
	auto username = scope.get_credentials()["username"].get<std::string>();

	Token s = TokenService::instance().getTokenByID(id);

	if (s.user != username)
		throw std::system_error(zeep::http::status_type::forbidden);

	TokenService::instance().deleteToken(id);

	return zeep::http::reply::stock_reply(zeep::http::status_type::ok);
}

zeep::http::reply UserHTMLController::createToken(const zeep::http::scope &scope, std::string name)
{
	auto credentials = scope.get_credentials();
	if (not credentials)
		throw zeep::http::unauthorized_exception();

	auto username = credentials["username"].get<std::string>();

	if (name.empty())
		name = "<untitled>";

	TokenService::instance().create(name, username);

	return zeep::http::reply::redirect("/token", zeep::http::status_type::see_other);
}

zeep::http::reply UserHTMLController::requestToken(const zeep::http::scope &scope, std::string name)
{
	auto credentials = scope.get_credentials();
	if (not credentials)
		throw zeep::http::unauthorized_exception();

	auto username = credentials["username"].get<std::string>();

	if (name.empty())
		name = "<untitled>";

	auto token = TokenService::instance().create(name, username);
	zeep::http::reply reply(zeep::http::status_type::ok);
	reply.set_content(zeep::el::serializer<Token>::serialize(token));

	return reply;
}
