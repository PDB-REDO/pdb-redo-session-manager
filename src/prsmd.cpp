//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <zeep/config.hpp>

#include <functional>
#include <tuple>
#include <thread>
#include <condition_variable>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/random/random_device.hpp>

#include <zeep/md5.hpp>
#include <zeep/base64.hpp>
#include <zeep/serialize.hpp>
#include <zeep/unicode_support.hpp>

#include <zeep/el/parser.hpp>
#include <zeep/http/webapp.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/rest/controller.hpp>

#include <pqxx/pqxx>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/cryptlib.h>
#include <cryptopp/osrng.h>
#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/hex.h>
#include <cryptopp/integer.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/modes.h>
#include <cryptopp/base64.h>
#include <cryptopp/gcm.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/md5.h>

#include "mrsrc.h"

namespace zh = zeep::http;
namespace el = zeep::el;
namespace fs = std::filesystem;
namespace ba = boost::algorithm;
namespace po = boost::program_options;
namespace pt = boost::posix_time;

using json = el::element;

// --------------------------------------------------------------------

#define APP_NAME "prsmd"

const std::string
	kPDB_REDO_Session_Realm("PDB-REDO Session Management"),
	kPDB_REDO_API_Realm("PDB-REDO API"),
	kPDB_REDO_API_Cookie("PDB-REDO-Signature"),
	kPDB_REDO_Cookie("PDB-REDO_Session");

const int
	kIterations = 10000,
	kSaltLength = 16,
	kKeyLength = 256;

fs::path gExePath;

// --------------------------------------------------------------------

std::string get_random_hash()
{
	boost::random::random_device rng;

	uint32_t data[4] = { rng(), rng(), rng(), rng() };

	return zeep::md5(data, sizeof(data)).finalise();
}

// --------------------------------------------------------------------

struct Session
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string realm;
	std::string token;
	boost::posix_time::ptime created;
	boost::posix_time::ptime expires;

	Session& operator=(const pqxx::tuple& t)
	{
		id			= t.at("id").as<unsigned long>();
		name		= t.at("name").as<std::string>();
		user		= t.at("user").as<std::string>();
		realm		= t.at("realm").as<std::string>();
		token		= t.at("token").as<std::string>();
		created		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("created").as<std::string>());
		expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const	{ return id != 0; }

	bool expired() const	{ return pt::second_clock::universal_time() > expires; }

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("realm", realm)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("expires", expires);
	}
};

struct SessionForApi
{
	unsigned long id = 0;
	std::string name;
	std::string token;
	boost::posix_time::ptime expires;

	SessionForApi(const pqxx::tuple& t)
		: id(t.at("id").as<unsigned long>())
		, name(t.at("name").as<std::string>())
		, token(t.at("token").as<std::string>())
		, expires(zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>()))
	{
	}

	SessionForApi& operator=(const Session& session)
	{
		id = session.id;
		name = session.name;
		token = session.token;
		expires = session.expires;
		
		return *this;
	}

	SessionForApi& operator=(const pqxx::tuple& t)
	{
		id			= t.at("id").as<unsigned long>();
		name		= t.at("name").as<std::string>();
		token		= t.at("token").as<std::string>();
		expires		= zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>());

		return *this;
	}

	operator bool() const	{ return id != 0; }

	bool expired() const	{ return pt::second_clock::universal_time() > expires; }

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("expires", expires);
	}
};

// --------------------------------------------------------------------

struct Run
{
	std::string name;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("name", name);
	}
};

// --------------------------------------------------------------------

class SessionStore
{
  public:

	static void init(pqxx::connection& connection)
	{
		sInstance = new SessionStore(connection);
	}

	static SessionStore& instance()
	{
		return *sInstance;
	}

	void stop()
	{
		delete sInstance;
		sInstance = nullptr;
	}

	Session create(const std::string& name, const std::string& user, const std::string& realm);

	Session get_by_id(unsigned long id);
	Session get_by_token(const std::string& token);
	void delete_by_id(unsigned long id);

  private:

	SessionStore(pqxx::connection& connection);
	~SessionStore();

	SessionStore(const SessionStore&) = delete;
	SessionStore& operator=(const SessionStore&) = delete;

	void run_clean_thread();

	pqxx::connection& m_connection;
	bool m_done = false;

	std::condition_variable m_cv;
	std::mutex m_cv_m;
	std::thread m_clean;

	static SessionStore* sInstance;
};

SessionStore* SessionStore::sInstance = nullptr;

SessionStore::SessionStore(pqxx::connection& connection)
	: m_connection(connection)
	, m_clean(std::bind(&SessionStore::run_clean_thread, this))
{
	m_connection.prepare("create-session",
		R"(INSERT INTO session (user_id, name, realm, token)
		   VALUES ($1, $2, $3, $4)
		   RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	m_connection.prepare("create-admin-session",
		R"(INSERT INTO session (user_id, name, realm, token, expires)
		   VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP + interval '15 minute')
		   RETURNING id, trim(both '"' from to_json(created)::text) AS created)");

	m_connection.prepare("get-session-by-id",
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.realm,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.id = $1)");

	m_connection.prepare("get-session-by-token",
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.realm,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.token = $1)");

	m_connection.prepare("get-user-id",
		R"(SELECT id FROM auth_user WHERE name = $1)");

	m_connection.prepare("delete-by-id",
		R"(DELETE FROM session WHERE id = $1)");

	m_connection.prepare("delete-expired",
		R"(DELETE FROM session WHERE CURRENT_TIMESTAMP > expires)");
}

SessionStore::~SessionStore()
{
	{
		std::lock_guard<std::mutex> lock(m_cv_m);
		m_done = true;
		m_cv.notify_one();
	}

	m_clean.join();
}

void SessionStore::run_clean_thread()
{
	while (not m_done)
	{
		std::unique_lock<std::mutex> lock(m_cv_m);
		if (m_cv.wait_for(lock, std::chrono::seconds(60)) == std::cv_status::timeout)
		{
			try
			{
				pqxx::transaction tx(m_connection);
				auto r = tx.prepared("delete-expired").exec();
				tx.commit();
			}
			catch (const std::exception& ex)
			{
				std::cerr << ex.what() << std::endl;
			}
		}
	}
}

Session SessionStore::create(const std::string& name, const std::string& user, const std::string& realm)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-user-id")(user).exec();

	if (r.empty() or r.size() != 1)
		throw std::runtime_error("Invalid username/password");

	std::string token = get_random_hash();
	unsigned long userid = r.front()[0].as<unsigned long>();

	auto stmt = realm == kPDB_REDO_Session_Realm ?
		tx.prepared("create-admin-session") :
		tx.prepared("create-session");

	r = stmt(userid)(name)(realm)(token).exec();

	if (r.empty() or r.size() != 1)
		throw std::runtime_error("Error creating session");
	
	unsigned long tokenid = r.front()[0].as<unsigned long>();
	std::string created = r.front()[1].as<std::string>();

	tx.commit();
	
	return
	{
		tokenid,
		name,
		user,
		realm,
		token,
		zeep::value_serializer<boost::posix_time::ptime>::from_string(created)
	};
}

Session SessionStore::get_by_id(unsigned long id)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-session-by-id")(id).exec();

	Session result = {};
	if (r.size() == 1)
		result = r.front();

	tx.commit();
	
	return result;
}

Session SessionStore::get_by_token(const std::string& token)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("get-session-by-token")(token).exec();

	Session result = {};
	if (r.size() == 1)
		result = r.front();

	tx.commit();
	
	return result;
}

void SessionStore::delete_by_id(unsigned long id)
{
	pqxx::transaction tx(m_connection);
	auto r = tx.prepared("delete-by-id")(id).exec();
	tx.commit();
}

// --------------------------------------------------------------------

class pdb_redo_api_authenticator : public zh::authentication_validation_base
{
  public:
	pdb_redo_api_authenticator(pqxx::connection& connection)
		: m_connection(connection) {}

	/// Validate the authorization, Throws unauthorized_exception in case of failure
	virtual std::string validate_authentication(const zh::request& req, const std::string& realm)
	{
		std::string authorization = req.get_header("Authorization");

		// PDB-REDO-api Credential=token-id/date/pdb-redo-apiv2,SignedHeaders=host;x-pdb-redo-content-sha256,Signature=xxxxx

		if (not ba::starts_with(authorization, "PDB-REDO-api "))
			throw zh::unauthorized_exception(false, realm);

		std::string name, nonce, date, signature;
		std::vector<std::string> signedHeaders;

		std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

		std::smatch m;
		if (not std::regex_search(authorization, m, re))
			throw zh::unauthorized_exception(false, realm);

		std::unique_lock<std::mutex> lock(m_mutex);

		pqxx::transaction tx(m_connection);

		std::vector<std::string> credentials;
		std::string credential = m[1].str();
		ba::split(credentials, credential, ba::is_any_of("/"));

		if (credentials.size() != 4 or credentials[3] != "apiv2")
			throw zh::unauthorized_exception(false, realm);
		name = credentials[0];
		nonce = credentials[1];
		date = credentials[2];

		// // get the secret

		// if (type == )


		// bool result = false;

		// if (not result)
		// 	throw std::runtime_error("Invalid username/password");

		// return info["username"];		
		// return "maarten";

		return name;
	}

	/// Add e.g. headers to reply for an unauthorized request
	virtual void add_headers(zh::reply& rep, const std::string& realm, bool stale)
	{
		// std::unique_lock<std::mutex> lock(m_mutex);

		// m_auth_info.push_back(auth_info(realm));

		// std::string challenge = m_auth_info.back().get_challenge();
		// if (stale)
		// 	challenge += ", stale=\"true\"";

		// rep.set_header("WWW-Authenticate", challenge);
	}

  private:
	std::mutex m_mutex;
	pqxx::connection& m_connection;
	// std::deque<auth_info> m_auth_info;
};


// --------------------------------------------------------------------


class my_rest_controller : public zh::rest_controller
{
  public:
	my_rest_controller(pqxx::connection& connection, const std::string& pdbRedoDir)
		: zh::rest_controller("ajax")
		, m_connection(connection)
		, m_pdb_redo_dir(pdbRedoDir)
	{
		// map_get_request("session/{user}", &my_rest_controller::get_all_sessions_for_user, "user");
		map_post_request("session", &my_rest_controller::post_session, "user", "password", "name");

		map_delete_request("session/{id}", kPDB_REDO_API_Realm, &my_rest_controller::delete_session, "id");

		map_get_request("session/{id}/run", kPDB_REDO_API_Realm, &my_rest_controller::get_all_runs, "id");
	}

	virtual bool validate_request(zh::request& req, zh::reply& rep, const std::string& realm)
	{
		if (realm != kPDB_REDO_API_Realm)
			return false;

		bool result = true;

		try
		{
			std::string authorization = req.get_header("Authorization");
			// PDB-REDO-api Credential=token-id/date/pdb-redo-apiv2,SignedHeaders=host;x-pdb-redo-content-sha256,Signature=xxxxx

			if (not ba::starts_with(authorization, "PDB-REDO-api "))
				throw zh::unauthorized_exception(false, kPDB_REDO_API_Realm);

			std::vector<std::string> signedHeaders;

			std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

			std::smatch m;
			if (not std::regex_search(authorization, m, re))
				throw zh::unauthorized_exception(false, kPDB_REDO_API_Realm);

			std::vector<std::string> credentials;
			std::string credential = m[1].str();
			ba::split(credentials, credential, ba::is_any_of("/"));

			if (credentials.size() != 3 or credentials[2] != "pdb-redo-api")
				throw zh::unauthorized_exception(false, kPDB_REDO_API_Realm);

			auto signature = zeep::decode_base64(m[3].str());

			// Validate the signature

			// canonical request

			std::vector<std::tuple<std::string,std::string>> params;
			for (auto& p: req.get_parameters())
				params.push_back(std::make_tuple(p.first, p.second));
			std::sort(params.begin(), params.end());
			std::ostringstream ps;
			auto n = params.size();
			for (auto& [name, value]: params)
			{
				ps << zeep::encode_url(name);
				if (not value.empty())
					ps << '=' << zeep::encode_url(value);
				if (n-- > 1)
					ps << '&';
			}

			using CryptoPP::SHA256;
			using CryptoPP::SecByteBlock;
			using CryptoPP::HMAC;
			using CryptoPP::StringSource;
			using CryptoPP::StringSink;
			using CryptoPP::Base64Encoder;
			using CryptoPP::HashFilter;

			SHA256 sha256;

			SecByteBlock b(SHA256::DIGESTSIZE);
			sha256.Update(reinterpret_cast<const byte*>(req.payload.c_str()), req.payload.length());
			sha256.Final(b);
			auto contentHash = zeep::encode_base64(std::string((char*)b.data(), b.m_size));

			std::ostringstream ss;
			ss << to_string(req.method) << std::endl
			   << req.get_pathname() << std::endl
			   << ps.str() << std::endl
			   << req.get_header("host") << std::endl
			   << contentHash;

			auto canonicalRequest = ss.str();

			sha256 = {};
			sha256.Update(reinterpret_cast<const byte*>(canonicalRequest.c_str()), canonicalRequest.length());
			sha256.Final(b);

			auto canonicalRequestHash = zeep::encode_base64(std::string((char*)b.data(), b.m_size));

			// string to sign
			auto timestamp = req.get_header("X-PDB_REDO-Date");

			std::ostringstream ss2;
			ss2 << "PDB-REDO-api" << std::endl
			   << timestamp << std::endl
			   << credential << std::endl
			   << canonicalRequestHash;
			auto stringToSign = ss2.str();

			auto tokenid = credentials[0];
			auto date = credentials[1];

			auto secret = SessionStore::instance().get_by_id(std::stoul(tokenid)).token;
			auto keyString = "PDB-REDO" + secret;

	        std::string key;
			HMAC<SHA256> hmac(reinterpret_cast<const byte*>(keyString.c_str()), keyString.length());
			StringSource(date, true, new HashFilter(hmac, new StringSink(key)));

			std::string mac;
			HMAC<SHA256> hmac2(reinterpret_cast<const byte*>(key.c_str()), key.length());
			StringSource(stringToSign, true, new HashFilter(hmac2, new StringSink(mac)));

			if (mac != signature)
				throw zh::unauthorized_exception(false, kPDB_REDO_API_Realm);
		}
		catch(const std::exception& e)
		{
			using namespace std::literals;

			rep.set_content(el::element({
				{ "error", e.what() }
			}));
			rep.set_status(zh::unauthorized);

			result = false;
		}

		return result;
	}

	// CRUD routines
	SessionForApi post_session(std::string user, std::string password, std::string name)
	{
		pqxx::transaction tx(m_connection);
		auto r = tx.prepared("get-password")(user).exec();

		if (r.empty() or r.size() != 1)
			throw std::runtime_error("Invalid username/password");
		
		std::string pw = r.front()[0].as<std::string>();

		bool result = false;

		using CryptoPP::SHA256;
		using CryptoPP::SecByteBlock;
		using CryptoPP::HMAC;
		using CryptoPP::SHA1;
		using CryptoPP::AES;


		if (pw[0] == '!')
		{
			const size_t kBufferSize = kSaltLength + kKeyLength / 8;

			byte b[kBufferSize] = {};
			CryptoPP::StringSource(pw.substr(1), true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(b, sizeof(b))));

			CryptoPP::PKCS5_PBKDF2_HMAC<SHA1> pbkdf;
			SecByteBlock recoveredkey(AES::DEFAULT_KEYLENGTH);

			pbkdf.DeriveKey(recoveredkey, recoveredkey.size(), 0x00, (byte*)password.c_str(), password.length(),
				b, kSaltLength, kIterations);
			
			SecByteBlock test(b + kSaltLength, CryptoPP::AES::DEFAULT_KEYLENGTH);

			result = recoveredkey == test;
		}
		else
		{
			CryptoPP::Weak::MD5 md5;

			SecByteBlock test(CryptoPP::Weak::MD5::BLOCKSIZE);
			CryptoPP::StringSource(pw, true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(test, test.size())));

			md5.Update(reinterpret_cast<const byte*>(password.c_str()), password.length());

			result = md5.Verify(test);
		}

		if (not result)
			throw std::runtime_error("Invalid username/password");

		std::string token = get_random_hash();
		unsigned long userid = r.front()[1].as<unsigned long>();

		auto t = tx.prepared("create-session")(userid)(name)(kPDB_REDO_API_Realm)(token).exec();

		tx.commit();

		return t.front();
	}

	std::string delete_session(unsigned long id)
	{
		SessionStore::instance().delete_by_id(id);

		return "ok";
	}

	std::vector<Session> get_all_sessions()
	{
		std::vector<Session> result;

		pqxx::transaction tx(m_connection);

		auto rows = tx.prepared("get-session-all").exec();
		for (auto row: rows)
		{
			Session session;
			session = row;
			result.push_back(std::move(session));
		}

		return result;
	}

	std::vector<Run> get_all_runs(unsigned long id)
	{
		auto session = SessionStore::instance().get_by_id(id);

		auto userDir = m_pdb_redo_dir / "runs" / session.user;

		std::vector<Run> result;

		if (fs::exists(userDir))
		{
			for (fs::directory_iterator run(userDir); run != fs::directory_iterator(); ++run)
			{
				if (not run->is_directory())
					continue;
				result.push_back({ run->path().filename().string() });
			}
		}

		return result;
	}

	// std::vector<Session> get_all_sessions_for_user(std::string user)
	// {
	// 	boost::uuids::uuid tag;
	// 	tag = boost::uuids::random_generator()();

	// 	std::ostringstream s;
	// 	s << tag;

	// 	return { { s.str(), user } };
	// }

  private:
	pqxx::connection& m_connection;
	fs::path m_pdb_redo_dir;
};

// --------------------------------------------------------------------

struct auth_info
{
	auth_info(const std::string& realm)
		: m_realm(realm)
	{
		m_nonce = get_random_hash();
		m_created = pt::second_clock::local_time();
	}

	std::string get_challenge() const
	{
		return R"(Basic-PDB_REDO realm=")" + m_realm + R"(", nonce=")" + m_nonce + '"';
	}

	// bool stale() const;

	std::string m_nonce, m_realm;
	// std::set<uint32_t> m_replay_check;
	pt::ptime m_created;
};

// bool auth_info::stale() const
// {
// 	pt::time_duration age = pt::second_clock::local_time() - m_created;
// 	return age.total_seconds() > 1800;
// }

// bool auth_info::validate(method_type method, const std::string& uri, const std::string& ha1, std::map<std::string, std::string>& info)
// {
// 	bool valid = false;

// 	uint32_t nc = strtol(info["nc"].c_str(), nullptr, 16);
// 	if (not m_replay_check.count(nc))
// 	{
// 		std::string ha2 = md5(to_string(method) + std::string{':'} + info["uri"]).finalise();

// 		std::string response = md5(
// 								   ha1 + ':' +
// 								   info["nonce"] + ':' +
// 								   info["nc"] + ':' +
// 								   info["cnonce"] + ':' +
// 								   info["qop"] + ':' +
// 								   ha2)
// 								   .finalise();

// 		valid = info["response"] == response;

// 		// keep a list of seen nc-values in m_replay_check
// 		m_replay_check.insert(nc);
// 	}
// 	return valid;
// }

class pdb_redo_authenticator : public zh::authentication_validation_base
{
  public:
	pdb_redo_authenticator(pqxx::connection& connection)
		: m_connection(connection) {}

	/// Validate the authorization, returns the validated user. Throws unauthorized_exception in case of failure
	virtual std::string validate_authentication(const zh::request& req, const std::string& realm)
	{
		auto session = SessionStore::instance().get_by_token(req.get_cookie(kPDB_REDO_Cookie));
		if (session and session.realm == realm and not session.expired())
		{
			return session.user;
		}

		std::string authorization = req.get_header("Authorization");

		// There are two options for the authorization string for pdb-redo-auth
		// The API version, using a token and the regular login one using a username/password
		
		// PDB-REDO-login Credential=username/nonce/date, Password=base64encodedpassword
		// PDB-REDO-api Credential=token-id/nonce/date/apiv2,SignedHeaders=host;x-pdb-redo-content-sha256,Signature=xxxxx

		enum ValidationType { Login, Api } type;

		if (ba::starts_with(authorization, "PDB-REDO-login "))
		{
			type = Login;
			authorization.erase(0, 15);
		}
		else if (ba::starts_with(authorization, "PDB-REDO-api "))
		{
			type = Api;
			authorization.erase(0, 13);
		}
		else
			throw zh::unauthorized_exception(false, realm);

		std::string name, nonce, date, signature;
		std::vector<std::string> signedHeaders;

		// Ahhh... std::regex does not support (?| | )
		// std::regex re(R"rx((\w+)=(?|"([^"]*)"|'([^']*)'|(\w+))(?:,\s*)?)rx");
		std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*(?:SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)|Password=(\S+))\s*)rx", std::regex::icase);

		std::smatch m;
		if (not std::regex_search(authorization, m, re))
			throw zh::unauthorized_exception(false, realm);

		std::unique_lock<std::mutex> lock(m_mutex);

		pqxx::transaction tx(m_connection);

		std::vector<std::string> credentials;
		std::string credential = m[1].str();
		ba::split(credentials, credential, ba::is_any_of("/"));

		if (type == Login)
		{
			if (credentials.size() != 3 or not m[4].matched)
				throw zh::unauthorized_exception(false, realm);
			name = credentials[0];
			nonce = credentials[1];
			date = credentials[2];

			std::string password = zeep::decode_base64(m[4].str());

			auto r = tx.prepared("get-password")(name).exec();

			if (r.empty() or r.size() != 1)
				throw std::runtime_error("Invalid username/password");
			
			std::string pw = r.front()[0].as<std::string>();

			bool valid = false;

			using CryptoPP::SecByteBlock;

			if (pw[0] == '!')
			{
				const size_t kBufferSize = kSaltLength + kKeyLength / 8;

				byte b[kBufferSize] = {};
				CryptoPP::StringSource(pw.substr(1), true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(b, sizeof(b))));

				CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA1> pbkdf;
				SecByteBlock recoveredkey(CryptoPP::AES::DEFAULT_KEYLENGTH);

				pbkdf.DeriveKey(recoveredkey, recoveredkey.size(), 0x00, (byte*)password.c_str(), password.length(),
					b, kSaltLength, kIterations);
				
				SecByteBlock test(b + kSaltLength, CryptoPP::AES::DEFAULT_KEYLENGTH);

				valid = recoveredkey == test;
			}
			else
			{
				CryptoPP::Weak::MD5 md5;

				SecByteBlock test(CryptoPP::Weak::MD5::BLOCKSIZE);
				CryptoPP::StringSource(pw, true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(test, test.size())));

				md5.Update(reinterpret_cast<const byte*>(password.c_str()), password.length());

				valid = md5.Verify(test);
			}

			if (not valid)
				throw zh::unauthorized_exception(false, realm);
		}
		else	// Api
		{
			if (credentials.size() != 4 or credentials[3] != "apiv2")
				throw zh::unauthorized_exception(false, realm);
			name = credentials[0];
			nonce = credentials[1];
			date = credentials[2];
		}

		// // get the secret

		// if (type == )


		// bool result = false;

		// if (not result)
		// 	throw std::runtime_error("Invalid username/password");

		// return info["username"];		
		// return "maarten";

		return name;
	}

	/// Add e.g. headers to reply for an unauthorized request
	virtual void add_headers(zh::reply& rep, const std::string& realm, bool stale)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_auth_info.push_back(auth_info(realm));

		std::string challenge = m_auth_info.back().get_challenge();
		if (stale)
			challenge += ", stale=\"true\"";

		rep.set_header("WWW-Authenticate", challenge);
	}

  private:
	std::mutex m_mutex;
	pqxx::connection& m_connection;
	std::deque<auth_info> m_auth_info;
};

// --------------------------------------------------------------------

#if DEBUG
class my_server : public zh::file_based_webapp
#else
class my_server : public zh::rsrc_based_webapp
#endif
{
  public:

	my_server(const std::string& dbConnectString, const std::string& admin, const std::string& pdbRedoDir)
#if DEBUG
		: zh::file_based_webapp((fs::current_path() / "docroot").string())
#else
		: zh::rsrc_based_webapp()
#endif
		, m_connection(dbConnectString)
		// : zh::webapp((fs::current_path() / "docroot").string())
		, m_rest_controller(new my_rest_controller(m_connection, pdbRedoDir))
		, m_pdb_redo_dir(pdbRedoDir)
	{
		SessionStore::init(m_connection);

		register_tag_processor<zh::tag_processor_v2>("http://www.hekkelman.com/libzeep/m2");

		add_controller(m_rest_controller);

		// get administrators
		ba::split(m_admins, admin, ba::is_any_of(",; "));

		set_authenticator(new pdb_redo_authenticator(m_connection));
	
		mount("", &my_server::welcome);
		mount("login", &my_server::login);
		mount("login-dialog", &my_server::loginDialog);
		mount("logout", &my_server::logout);
		mount("admin", kPDB_REDO_Session_Realm, &my_server::admin);

		mount("css", &my_server::handle_file);
		mount("scripts", &my_server::handle_file);
		mount("fonts", &my_server::handle_file);
		mount("images", &my_server::handle_file);

		m_connection.prepare("get-password", "SELECT password, id FROM auth_user WHERE name = $1");

		m_connection.prepare("get-session-all",
			R"(SELECT a.id AS id,
				trim(both '"' from to_json(a.created)::text) AS created,
				trim(both '"' from to_json(a.expires)::text) AS expires,
				a.name AS name,
				b.name AS user,
				a.token AS token,
				a.realm as realm
			   FROM session a, auth_user b
			   WHERE a.user_id = b.id
			   ORDER BY a.created ASC)");

	}

	~my_server()
	{
		SessionStore::instance().stop();
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void admin(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void login(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void loginDialog(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void logout(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	void create_unauth_reply(const zh::request& req, bool stale, const std::string& realm, zh::reply& rep);

  private:
	pqxx::connection m_connection;
	my_rest_controller*	m_rest_controller;
	std::set<std::string> m_admins;
	std::string m_pdb_redo_dir;
};

void my_server::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	auto session = SessionStore::instance().get_by_token(request.get_cookie(kPDB_REDO_Cookie));

	if (session)
	{
		sub.put("username", session.user);
	}

	create_reply_from_template("index.html", sub, reply);
}

void my_server::admin(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	if (m_admins.count(request.username) == 0)
		throw std::runtime_error("Insufficient priviliges to access admin page");

	zh::scope sub(scope);

	auto v = m_rest_controller->get_all_sessions();
	json sessions;
	zeep::to_element(sessions, v);
	sub.put("sessions", sessions);

	create_reply_from_template("admin.html", sub, reply);
}

void my_server::login(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	std::string username = request.get_parameter("username");
	std::string password = request.get_parameter("password");

	pqxx::transaction tx(m_connection);

	auto r = tx.prepared("get-password")(username).exec();

	if (r.empty() or r.size() != 1)
		throw zh::unauthorized_exception(false, kPDB_REDO_Session_Realm);
			
	std::string pw = r.front()[0].as<std::string>();

	tx.commit();

	bool valid = false;

	using CryptoPP::SecByteBlock;

	if (pw[0] == '!')
	{
		const size_t kBufferSize = kSaltLength + kKeyLength / 8;

		byte b[kBufferSize] = {};
		CryptoPP::StringSource(pw.substr(1), true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(b, sizeof(b))));

		CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA1> pbkdf;
		SecByteBlock recoveredkey(CryptoPP::AES::DEFAULT_KEYLENGTH);

		pbkdf.DeriveKey(recoveredkey, recoveredkey.size(), 0x00, (byte*)password.c_str(), password.length(),
			b, kSaltLength, kIterations);
		
		SecByteBlock test(b + kSaltLength, CryptoPP::AES::DEFAULT_KEYLENGTH);

		valid = recoveredkey == test;
	}
	else
	{
		CryptoPP::Weak::MD5 md5;

		SecByteBlock test(CryptoPP::Weak::MD5::BLOCKSIZE);
		CryptoPP::StringSource(pw, true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(test, test.size())));

		md5.Update(reinterpret_cast<const byte*>(password.c_str()), password.length());

		valid = md5.Verify(test);
	}

	if (not valid)
		throw zh::unauthorized_exception(false, kPDB_REDO_Session_Realm);

	// so the user was authenticated. Acknowledge and set cookie
	auto session = SessionStore::instance().create("", username, kPDB_REDO_Session_Realm);

	auto uri = request.get_parameter("uri");

	if (uri.empty())
		reply = zh::reply::stock_reply(zh::ok);
	else
		reply = zh::reply::redirect(uri);
	
	reply.set_header("Set-Cookie", kPDB_REDO_Cookie + "=" + session.token);// + "; Secure");
}

void my_server::loginDialog(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	create_reply_from_template("login::#login-dialog", scope, reply);
}

// --------------------------------------------------------------------

void my_server::logout(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	auto session = SessionStore::instance().get_by_token(request.get_cookie(kPDB_REDO_Cookie));

	if (session)
		SessionStore::instance().delete_by_id(session.id);

	reply = zh::reply::redirect("/");
	reply.set_header("Set-Cookie", kPDB_REDO_Cookie + "; Expires=Thu, 01 Jan 1970 00:00:01 GMT;");
}

// --------------------------------------------------------------------

void my_server::create_unauth_reply(const zh::request& req, bool stale, const std::string& realm, zh::reply& reply)
{
	zh::scope scope;

	scope.put("uri", req.uri);

	create_reply_from_template("login.html", scope, reply);

	reply.set_status(zh::unauthorized);
}

// --------------------------------------------------------------------

int main(int argc, const char* argv[])
{
	using namespace std::literals;

	int result = 0;

	po::options_description visible(argv[0] + " [options] command"s);
	visible.add_options()
		("help,h",										"Display help message")
		("verbose,v",									"Verbose output")
		("no-daemon,F",									"Do not fork into background")
		;
	
	po::options_description config(APP_NAME R"( config file options)");
	
	config.add_options()
		("pdb-redo-dir",	po::value<std::string>(),	"Directory containing PDB-REDO server data")
		("address",			po::value<std::string>(),	"External address, default is 0.0.0.0")
		("port",			po::value<uint16_t>(),		"Port to listen to, default is 10339")
		("user,u",			po::value<std::string>(),	"User to run the daemon")
		("db-host",			po::value<std::string>(),	"Database host")
		("db-port",			po::value<std::string>(),	"Database port")
		("db-dbname",		po::value<std::string>(),	"Database name")
		("db-user",			po::value<std::string>(),	"Database user name")
		("db-password",		po::value<std::string>(),	"Database password")
		("admin",			po::value<std::string>(),	"Administrators, list of usernames separated by comma");

	po::options_description hidden("hidden options");
	hidden.add_options()
		("command",			po::value<std::string>(),	"Command, one of start, stop, status or reload")
		("debug,d",			po::value<int>(),			"Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	p.add("command", 1);

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	fs::path configFile = APP_NAME ".conf";
#warning("pick up from /etc?")
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / APP_NAME ".conf";
	
	if (vm.count("config") != 0)
	{
		configFile = vm["config"].as<std::string>();
		if (not fs::exists(configFile))
			throw std::runtime_error("Specified config file does not seem to exist");
	}
	
	if (fs::exists(configFile))
	{
		po::options_description config_options ;
		config_options.add(config).add(hidden);

		std::ifstream cfgFile(configFile);
		if (cfgFile.is_open())
			po::store(po::parse_config_file(cfgFile, config_options), vm);
	}
	
	po::notify(vm);

	// --------------------------------------------------------------------

	if (vm.count("help") or vm.count("command") == 0)
	{
		std::cerr << visible << std::endl
			 << R"(
Command should be either:

  start     start a new server
  stop      start a running server
  status    get the status of a running server
  reload    restart a running server with new options
			 )" << std::endl;
		exit(vm.count("help") ? 0 : 1);
	}
	
	if (vm.count("pdb-redo-dir") == 0)
	{
		std::cerr << "Missing pdb-redo-dir option" << std::endl;
		exit(1);
	}

	try
	{
		char exePath[PATH_MAX + 1];
		int r = readlink("/proc/self/exe", exePath, PATH_MAX);
		if (r > 0)
		{
			exePath[r] = 0;
			gExePath = fs::weakly_canonical(exePath);
		}
		
		if (not fs::exists(gExePath))
			gExePath = fs::weakly_canonical(argv[0]);

		std::vector<std::string> vConn;
		for (std::string opt: { "db-host", "db-port", "db-dbname", "db-user", "db-password" })
		{
			if (vm.count(opt) == 0)
				continue;
			
			vConn.push_back(opt.substr(3) + "=" + vm[opt].as<std::string>());
		}

		std::string admin = vm["admin"].as<std::string>();
		std::string pdbRedoDir = vm["pdb-redo-dir"].as<std::string>();

		zh::daemon server([cs = ba::join(vConn, " "), admin, pdbRedoDir]()
		{
			return new my_server(cs, admin, pdbRedoDir);
		}, APP_NAME );

		std::string user = "www-data";
		if (vm.count("user") != 0)
			user = vm["user"].as<std::string>();
		
		std::string address = "0.0.0.0";
		if (vm.count("address"))
			address = vm["address"].as<std::string>();

		uint16_t port = 10339;
		if (vm.count("port"))
			port = vm["port"].as<uint16_t>();

		std::string command = vm["command"].as<std::string>();

		if (command == "start")
		{
			if (vm.count("no-daemon"))
				result = server.run_foreground(address, port);
			else
				result = server.start(address, port, 2, user);
			// server.start(vm.count("no-daemon"), address, port, 2, user);
			// // result = daemon::start(vm.count("no-daemon"), port, user);
		}
		else if (command == "stop")
			result = server.stop();
		else if (command == "status")
			result = server.status();
		else if (command == "reload")
			result = server.reload();
		else
		{
			std::cerr << "Invalid command" << std::endl;
			result = 1;
		}
	}
	catch (const std::exception &ex)
	{
		std::cerr << "exception:" << std::endl
			 << ex.what() << std::endl;
		result = 1;
	}

	return result;
}