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

#include <zeep/crypto.hpp>
#include <zeep/serialize.hpp>
#include <zeep/unicode_support.hpp>

#include <zeep/el/parser.hpp>
#include <zeep/http/webapp.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/rest/controller.hpp>

#include <pqxx/pqxx>

#include "user-service.hpp"
#include "run-service.hpp"
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

struct Session
{
	unsigned long id = 0;
	std::string name;
	std::string user;
	std::string token;
	boost::posix_time::ptime created;
	boost::posix_time::ptime expires;

	Session& operator=(const pqxx::tuple& t)
	{
		id			= t.at("id").as<unsigned long>();
		name		= t.at("name").as<std::string>();
		user		= t.at("user").as<std::string>();
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
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("created", created)
		   & zeep::make_nvp("expires", expires);
	}
};

struct CreateSessionResult
{
	unsigned long id = 0;
	std::string name;
	std::string token;
	boost::posix_time::ptime expires;

	CreateSessionResult(const Session& session)
		: id(session.id)
		, name(session.name)
		, token(session.token)
		, expires(session.expires)
	{
	}

	CreateSessionResult(const pqxx::tuple& t)
		: id(t.at("id").as<unsigned long>())
		, name(t.at("name").as<std::string>())
		, token(t.at("token").as<std::string>())
		, expires(zeep::value_serializer<boost::posix_time::ptime>::from_string(t.at("expires").as<std::string>()))
	{
	}

	CreateSessionResult& operator=(const Session& session)
	{
		id = session.id;
		name = session.name;
		token = session.token;
		expires = session.expires;
		
		return *this;
	}

	CreateSessionResult& operator=(const pqxx::tuple& t)
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

	Session create(const std::string& name, const std::string& user);

	Session get_by_id(unsigned long id);
	Session get_by_token(const std::string& token);
	void delete_by_id(unsigned long id);

	std::vector<Session> get_all_sessions();

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
		R"(INSERT INTO session (user_id, name, token)
		   VALUES ($1, $2, $3)
		   RETURNING id, name, token, trim(both '"' from to_json(created)::text) AS created,
			   trim(both '"' from to_json(expires)::text) AS expires)");

	m_connection.prepare("create-admin-session",
		R"(INSERT INTO session (user_id, name, token, expires)
		   VALUES ($1, $2, $3, CURRENT_TIMESTAMP + interval '15 minute')
		   RETURNING id, trim(both '"' from to_json(created)::text) AS created)");

	m_connection.prepare("get-session-by-id",
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.id = $1)");

	m_connection.prepare("get-session-by-token",
		R"(SELECT a.id,
				  a.name AS name,
				  b.name AS user,
				  a.token,
				  trim(both '"' from to_json(a.created)::text) AS created,
				  trim(both '"' from to_json(a.expires)::text) AS expires
		   FROM session a LEFT JOIN auth_user b ON a.user_id = b.id
		   WHERE a.token = $1)");

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

Session SessionStore::create(const std::string& name, const std::string& user)
{
	User u = UserService::instance().get_user(user);

	pqxx::transaction tx(m_connection);

	std::string token = zeep::encode_base64url(zeep::random_hash());

	auto r = tx.prepared("create-session")(u.id)(name)(token).exec();

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

std::vector<Session> SessionStore::get_all_sessions()
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

// --------------------------------------------------------------------

class session_rest_controller : public zh::rest_controller
{
  public:
	session_rest_controller(pqxx::connection& connection, const std::string& pdbRedoDir)
		: zh::rest_controller("api")
		, m_connection(connection)
		, m_pdb_redo_dir(pdbRedoDir)
	{
		// create a new session, user should provide username, password and session name
		map_post_request("session", &session_rest_controller::post_session, "user", "password", "name");

		// get session info
		map_get_request("session/{id}", kPDB_REDO_API_Realm, &session_rest_controller::get_session, "id");

		// delete a session
		map_delete_request("session/{id}", kPDB_REDO_API_Realm, &session_rest_controller::delete_session, "id");

		// return a list of runs
		map_get_request("session/{id}/run", kPDB_REDO_API_Realm, &session_rest_controller::get_all_runs, "id");

		// // return info for a run
		// map_get_request("session/{id}/run/{run}", kPDB_REDO_API_Realm, &session_rest_controller::get_run, "id", "run");

		// Submit a run (job)
		map_post_request("session/{id}/run", kPDB_REDO_API_Realm, &session_rest_controller::create_job, "id",
			"mtz-file", "pdb-file", "restraints-file", "sequence-file");
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
				throw zh::unauthorized_exception(kPDB_REDO_API_Realm);

			std::vector<std::string> signedHeaders;

			std::regex re(R"rx(Credential=("[^"]*"|'[^']*'|[^,]+),\s*SignedHeaders=("[^"]*"|'[^']*'|[^,]+),\s*Signature=("[^"]*"|'[^']*'|[^,]+)\s*)rx", std::regex::icase);

			std::smatch m;
			if (not std::regex_search(authorization, m, re))
				throw zh::unauthorized_exception(kPDB_REDO_API_Realm);

			std::vector<std::string> credentials;
			std::string credential = m[1].str();
			ba::split(credentials, credential, ba::is_any_of("/"));

			if (credentials.size() != 3 or credentials[2] != "pdb-redo-api")
				throw zh::unauthorized_exception(kPDB_REDO_API_Realm);

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

			auto contentHash = zeep::encode_base64(zeep::sha256(req.payload));

			std::ostringstream ss;
			ss << to_string(req.method) << std::endl
			   << req.get_pathname() << std::endl
			   << ps.str() << std::endl
			   << req.get_header("host") << std::endl
			   << contentHash;

			auto canonicalRequest = ss.str();
			auto canonicalRequestHash = zeep::encode_base64(zeep::sha256(canonicalRequest));

			// string to sign
			auto timestamp = req.get_header("X-PDB-REDO-Date");

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

			auto key = zeep::hmac_sha256(date, keyString);
			if (zeep::hmac_sha256(stringToSign, key) != signature)
				throw zh::unauthorized_exception(kPDB_REDO_API_Realm);
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
	CreateSessionResult post_session(std::string user, std::string password, std::string name)
	{
		pqxx::transaction tx(m_connection);
		auto r = tx.prepared("get-password")(user).exec();

		if (r.empty() or r.size() != 1)
			throw std::runtime_error("Invalid username/password");
		
		std::string pw = r.front()[0].as<std::string>();

		bool result = false;

		if (pw[0] == '!')
		{
			auto pb = zeep::decode_base64(pw.substr(1));
			auto salt = pb.substr(0, kSaltLength);

			auto test = zeep::pbkdf2_hmac_sha1(salt, password.c_str(), kIterations, kKeyLength / 8);
			result = test == pb.substr(kSaltLength);
		}
		else
			result = zeep::md5(password) == zeep::decode_base64(pw);

		if (not result)
			throw std::runtime_error("Invalid username/password");

		std::string token = zeep::encode_base64url(zeep::random_hash());
		unsigned long userid = r.front()[1].as<unsigned long>();

		auto t = tx.prepared("create-session")(userid)(name)(token).exec();

		tx.commit();

		return t.front();
	}

	CreateSessionResult get_session(unsigned long id)
	{
		return SessionStore::instance().get_by_id(id);
	}

	void delete_session(unsigned long id)
	{
		SessionStore::instance().delete_by_id(id);
	}

	std::vector<Run> get_all_runs(unsigned long id)
	{
		auto session = SessionStore::instance().get_by_id(id);

		return RunService::instance().get_runs_for_user(session.user);
	}

	// std::vector<Session> get_all_sessions_for_user(std::string user)
	// {
	// 	boost::uuids::uuid tag;
	// 	tag = boost::uuids::random_generator()();

	// 	std::ostringstream s;
	// 	s << tag;

	// 	return { { s.str(), user } };
	// }

	Run create_job(unsigned long sessionID, const zh::file_param& diffractionData, const zh::file_param& coordinates,
		const zh::file_param& restraints, const zh::file_param& sequence)
	{
		auto session = SessionStore::instance().get_by_id(sessionID);

		return RunService::instance().submit(session.user, coordinates, diffractionData, restraints, sequence, {});
	}

  private:
	pqxx::connection& m_connection;
	fs::path m_pdb_redo_dir;
};

// --------------------------------------------------------------------


class pdb_redo_authenticator : public zh::jws_authentication_validation_base
{
  public:
	pdb_redo_authenticator(pqxx::connection& connection, const std::string& secret, const std::string& admins)
		: jws_authentication_validation_base(kPDB_REDO_Session_Realm, secret)
		, m_connection(connection)
	{
		ba::split(m_admins, admins, ba::is_any_of(",; "));
	}

	virtual el::element validate_username_password(const std::string& username, const std::string& password)
	{
		el::element credentials;

		for (;;)
		{
			pqxx::transaction tx(m_connection);

			auto r = tx.prepared("get-password")(username).exec();

			if (r.empty() or r.size() != 1)
				break;
					
			std::string pw = r.front()[0].as<std::string>();

			tx.commit();

			if (pw[0] == '!')
			{
				std::string b = zeep::decode_base64(pw.substr(1));
				std::string test = zeep::pbkdf2_hmac_sha1(b.substr(0, kSaltLength), password, kIterations, kKeyLength / 8);

				if (b.substr(kSaltLength) != test)
					break;
			}
			else if (zeep::encode_base64(zeep::md5(password)) != pw)
				break;

			credentials["username"] = username;
			credentials["admin"] = m_admins.count(username);
			break;
		}

		return credentials;
	}

  private:
	pqxx::connection& m_connection;
	std::set<std::string> m_admins;
};

// --------------------------------------------------------------------

#if DEBUG
using session_server_base = zh::file_based_webapp;
#else
using session_server_base = zh::rsrc_based_webapp;
#endif

class session_server : public session_server_base
{
  public:

	session_server(const std::string& dbConnectString, const std::string& admin, const std::string& pdbRedoDir, const std::string& secret)
		: session_server_base((fs::current_path() / "docroot").string())
		, m_connection(dbConnectString)
		, m_pdb_redo_dir(pdbRedoDir)
	{
		UserService::init(m_connection);
		SessionStore::init(m_connection);

		register_tag_processor<zh::tag_processor_v2>("http://www.hekkelman.com/libzeep/m2");

		add_controller(new session_rest_controller(m_connection, pdbRedoDir));

		// get administrators
		ba::split(m_admins, admin, ba::is_any_of(",; "));

		set_authenticator(new pdb_redo_authenticator(m_connection, secret, admin), true);
	
		mount("", &session_server::welcome);
		mount("login-dialog", &session_server::loginDialog);
		mount("admin", kPDB_REDO_Session_Realm, &session_server::admin);

		mount("{css,scripts,fonts,images}/", &session_server::handle_file);

		mount("admin/deleteSession", kPDB_REDO_Session_Realm, zh::method_type::DELETE, &session_server::handle_delete_session);

		m_connection.prepare("get-password", "SELECT password, id FROM auth_user WHERE name = $1");

		m_connection.prepare("get-session-all",
			R"(SELECT a.id AS id,
				trim(both '"' from to_json(a.created)::text) AS created,
				trim(both '"' from to_json(a.expires)::text) AS expires,
				a.name AS name,
				b.name AS user,
				a.token AS token
			   FROM session a, auth_user b
			   WHERE a.user_id = b.id
			   ORDER BY a.created ASC)");

	}

	~session_server()
	{
		SessionStore::instance().stop();
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void admin(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void loginDialog(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	void handle_delete_session(const zh::request& request, const zh::scope& scope, zh::reply& reply);

  private:
	pqxx::connection m_connection;
	std::set<std::string> m_admins;
	std::string m_pdb_redo_dir;
};

void session_server::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	auto credentials = scope.lookup("credentials");
	if (credentials)
		sub.put("username", credentials["username"]);

	create_reply_from_template("index.html", sub, reply);
}

void session_server::admin(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	if (m_admins.count(request.username) == 0)
		throw std::runtime_error("Insufficient priviliges to access admin page");

	zh::scope sub(scope);

	json sessions;
	auto s = SessionStore::instance().get_all_sessions();
	zeep::to_element(sessions, s);
	sub.put("sessions", sessions);

	create_reply_from_template("admin.html", sub, reply);
}

void session_server::loginDialog(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	create_reply_from_template("login::#login-dialog", scope, reply);
}

void session_server::handle_delete_session(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	if (m_admins.count(request.username) == 0)
		throw std::runtime_error("Insufficient priviliges to access admin page");

	unsigned long sessionID = std::stoul(request.get_parameter("sessionid", "0"));
	if (sessionID != 0)
		SessionStore::instance().delete_by_id(sessionID);

	reply = zh::reply::stock_reply(zh::ok);
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
		("runs-dir",		po::value<std::string>(),	"Directory containing PDB-REDO server run directories")
		("address",			po::value<std::string>(),	"External address, default is 0.0.0.0")
		("port",			po::value<uint16_t>(),		"Port to listen to, default is 10339")
		("user,u",			po::value<std::string>(),	"User to run the daemon")
		("db-host",			po::value<std::string>(),	"Database host")
		("db-port",			po::value<std::string>(),	"Database port")
		("db-dbname",		po::value<std::string>(),	"Database name")
		("db-user",			po::value<std::string>(),	"Database user name")
		("db-password",		po::value<std::string>(),	"Database password")
		("admin",			po::value<std::string>(),	"Administrators, list of usernames separated by comma")
		("secret",			po::value<std::string>(),	"Secret value, used in signing access tokens");

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
		std::string runsDir = pdbRedoDir + "/runs";
		if (vm.count("runs-dir"))
			runsDir = vm["runs-dir"].as<std::string>();

		RunService::init(runsDir);
		
		std::string secret;
		if (vm.count("secret"))
			secret = vm["secret"].as<std::string>();
		else
		{
			secret = zeep::encode_base64(zeep::random_hash());
			std::cerr << "starting with created secret " << secret << std::endl;
		}

		zh::daemon server([cs = ba::join(vConn, " "), secret, admin, pdbRedoDir]()
		{
			return new session_server(cs, admin, pdbRedoDir, secret);
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