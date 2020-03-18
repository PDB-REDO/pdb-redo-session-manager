//               Copyright Maarten L. Hekkelman.
//   Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE_1_0.txt or copy at
//            http://www.boost.org/LICENSE_1_0.txt)

#include <zeep/config.hpp>

#include <functional>
#include <tuple>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/local_time/local_time.hpp>

#include <zeep/http/webapp.hpp>
#include <zeep/http/md5.hpp>

#include <zeep/el/parser.hpp>
#include <zeep/rest/controller.hpp>
#include <zeep/http/daemon.hpp>
#include <zeep/serialize.hpp>

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

using json = el::element;

// --------------------------------------------------------------------

#define APP_NAME "prsmd"

const int kIterations = 10000, kSaltLength = 16, kKeyLength = 256;

fs::path gExePath;

// --------------------------------------------------------------------

struct Session
{
	std::string id;
	std::string name;
	std::string user;
	std::string token;
	boost::posix_time::ptime created;

	template<typename Archive>
	void serialize(Archive& ar, unsigned long version)
	{
		ar & zeep::make_nvp("id", id)
		   & zeep::make_nvp("name", name)
		   & zeep::make_nvp("user", user)
		   & zeep::make_nvp("token", token)
		   & zeep::make_nvp("created", created);
	}
};

class my_rest_controller : public zh::rest_controller
{
  public:
	my_rest_controller(const std::string& connectionString)
		: zh::rest_controller("ajax")
		, m_connection(connectionString)
	{
		map_get_request("session/{user}", &my_rest_controller::get_all_sessions_for_user, "user");
		map_post_request("session", &my_rest_controller::post_session, "user", "password", "name");

		m_connection.prepare("get-password", "SELECT password, id FROM auth_user WHERE name = $1");

		m_connection.prepare("get-session-all",
			R"(SELECT a.id AS id,
				trim(both '"' from to_json(a.created)::text) AS created,
				a.name AS session_name,
				b.name AS user_name,
				a.token AS token
			   FROM session a, auth_user b
			   WHERE a.user_id = b.id
			   ORDER BY a.created ASC)");

		m_connection.prepare("create-session",
			R"(INSERT INTO session (user_id, name, token) values ($1, $2, $3) )");

		// m_connection.prepare("get-opname",
		// 	"SELECT a.id AS id, a.tijd AS tijd, b.teller_id AS teller_id, b.stand AS stand"
		// 	" FROM opname a, tellerstand b"
		// 	" WHERE a.id = b.opname_id AND a.id = $1");

		// m_connection.prepare("get-last-opname",
		// 	"SELECT a.id AS id, a.tijd AS tijd, b.teller_id AS teller_id, b.stand AS stand"
		// 	" FROM opname a, tellerstand b"
		// 	" WHERE a.id = b.opname_id AND a.id = (SELECT MAX(id) FROM opname)");

		// m_connection.prepare("insert-opname", "INSERT INTO opname DEFAULT VALUES RETURNING id");
		// m_connection.prepare("insert-stand", "INSERT INTO tellerstand (opname_id, teller_id, stand) VALUES($1, $2, $3);");

		// m_connection.prepare("update-stand", "UPDATE tellerstand SET stand = $1 WHERE opname_Id = $2 AND teller_id = $3;");

		// m_connection.prepare("del-opname", "DELETE FROM opname WHERE id=$1");

		// m_connection.prepare("get-tellers-all",
		// 	"SELECT id, naam, naam_kort, schaal FROM teller ORDER BY id");
	}

	// CRUD routines
	std::string post_session(std::string user, std::string password, std::string name)
	{
		pqxx::transaction tx(m_connection);
		auto r = tx.prepared("get-password")(user).exec();

		if (r.empty() or r.size() != 1)
			throw std::runtime_error("Invalid username/password");
		
		std::string pw = r.front()[0].as<std::string>();

		bool result = false;

		if (pw[0] == '!')
		{
			const size_t kBufferSize = kSaltLength + kKeyLength / 8;

			byte b[kBufferSize] = {};
			CryptoPP::StringSource(pw.substr(1), true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(b, sizeof(b))));

			CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA1> pbkdf;
			CryptoPP::SecByteBlock recoveredkey(CryptoPP::AES::DEFAULT_KEYLENGTH);

			pbkdf.DeriveKey(recoveredkey, recoveredkey.size(), 0x00, (byte*)password.c_str(), password.length(),
				b, kSaltLength, kIterations);
			
			CryptoPP::SecByteBlock test(b + kSaltLength, CryptoPP::AES::DEFAULT_KEYLENGTH);

			result = recoveredkey == test;
		}
		else
		{
			CryptoPP::Weak::MD5 md5;

			CryptoPP::SecByteBlock test(CryptoPP::Weak::MD5::BLOCKSIZE);
			CryptoPP::StringSource(pw, true, new CryptoPP::Base64Decoder(new CryptoPP::ArraySink(test, test.size())));

			md5.Update(reinterpret_cast<const byte*>(password.c_str()), password.length());

			result = md5.Verify(test);
		}

		if (not result)
			throw std::runtime_error("Invalid username/password");

		boost::uuids::uuid tag;
		tag = boost::uuids::random_generator()();

		std::string token = boost::lexical_cast<std::string>(tag);
		unsigned long userid = r.front()[1].as<unsigned long>();

		tx.prepared("create-session")(userid)(name)(token).exec();

		tx.commit();

		return token;
	}

	// void put_opname(string opnameId, Opname opname)
	// {
	// 	pqxx::transaction tx(m_connection);

	// 	for (auto stand: opname.standen)
	// 		tx.prepared("update-stand")(stand.second)(opnameId)(stol(stand.first)).exec();

	// 	tx.commit();
	// }

	// Opname get_opname(string id)
	// {
	// 	pqxx::transaction tx(m_connection);
	// 	auto rows = tx.prepared("get-opname")(id).exec();

	// 	if (rows.empty())
	// 		throw runtime_error("opname niet gevonden");

	// 	Opname result{ rows.front()[0].as<string>(), rows.front()[1].as<string>() };

	// 	for (auto row: rows)
	// 		result.standen[row[2].as<string>()] = row[3].as<float>();
		
	// 	return result;
	// }

	// Opname get_last_opname()
	// {
	// 	pqxx::transaction tx(m_connection);
	// 	auto rows = tx.prepared("get-last-opname").exec();

	// 	if (rows.empty())
	// 		throw runtime_error("opname niet gevonden");

	// 	Opname result{ rows.front()[0].as<string>(), rows.front()[1].as<string>() };

	// 	for (auto row: rows)
	// 		result.standen[row[2].as<string>()] = row[3].as<float>();
		
	// 	return result;
	// }

	// vector<Opname> get_all_opnames()
	// {
	// 	vector<Opname> result;

	// 	pqxx::transaction tx(m_connection);

	// 	auto rows = tx.prepared("get-opname-all").exec();
	// 	for (auto row: rows)
	// 	{
	// 		auto id = row[0].as<string>();

	// 		if (result.empty() or result.back().id != id)
	// 			result.push_back({id, row[1].as<string>()});

	// 		result.back().standen[row[2].as<string>()] = row[3].as<float>();
	// 	}

	// 	return result;
	// }

	std::vector<Session> get_all_sessions()
	{
		std::vector<Session> result;

		pqxx::transaction tx(m_connection);

		auto rows = tx.prepared("get-session-all").exec();
		for (auto row: rows)
		{
			Session session;
			session.id = row[0].as<std::string>();
			session.created = zeep::value_serializer<boost::posix_time::ptime>::from_string(row[1].as<std::string>());
			session.name = row[2].as<std::string>();
			session.user = row[3].as<std::string>();
			session.token = row[4].as<std::string>();
			result.push_back(std::move(session));
		}

		return result;
	}

	std::vector<Session> get_all_sessions_for_user(std::string user)
	{
		boost::uuids::uuid tag;
		tag = boost::uuids::random_generator()();

		std::ostringstream s;
		s << tag;

		return { { s.str(), user } };
	}

	// void delete_opname(string id)
	// {
	// 	pqxx::transaction tx(m_connection);
	// 	tx.prepared("del-opname")(id).exec();
	// 	tx.commit();
	// }

	// vector<Teller> get_tellers()
	// {
	// 	vector<Teller> result;

	// 	pqxx::transaction tx(m_connection);

	// 	auto rows = tx.prepared("get-tellers-all").exec();
	// 	for (auto row: rows)
	// 	{
	// 		auto c1 = row.column_number("id");
	// 		auto c2 = row.column_number("naam");
	// 		auto c3 = row.column_number("naam_kort");
	// 		auto c4 = row.column_number("schaal");

	// 		result.push_back({row[c1].as<string>(), row[c2].as<string>(), row[c3].as<string>(), row[c4].as<int>()});
	// 	}

	// 	return result;
	// }

	// // GrafiekData get_grafiek(const string& type, aggregatie_type aggregatie);
	// GrafiekData get_grafiek(grafiek_type type, aggregatie_type aggregatie);

  private:
	pqxx::connection m_connection;
};

// --------------------------------------------------------------------

#if DEBUG
class my_server : public zh::file_based_webapp
#else
class my_server : public zh::rsrc_based_webapp
#endif
{
  public:

	static const std::string kRealm;

	my_server(const std::string& dbConnectString, const std::string& admin, const std::string& pw)
#if DEBUG
		: zh::file_based_webapp((fs::current_path() / "docroot").string()),
#else
		:
#endif
		// : zh::webapp((fs::current_path() / "docroot").string())
		m_rest_controller(new my_rest_controller(dbConnectString))
		, m_admin(admin), m_pw(pw)
	{
		register_tag_processor<zh::tag_processor_v2>("http://www.hekkelman.com/libzeep/m2");

		add_controller(m_rest_controller);
	
		mount("", &my_server::welcome);
		mount("session", &my_server::welcome);

		mount("admin", kRealm, &my_server::admin);
		// mount("opnames", &my_server::opname);
		// mount("invoer", &my_server::invoer);
		// mount("grafiek", &my_server::grafiek);
		mount("css", &my_server::handle_file);
		mount("scripts", &my_server::handle_file);
		mount("fonts", &my_server::handle_file);
		mount("images", &my_server::handle_file);
	}

	void welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply);
	void admin(const zh::request& request, const zh::scope& scope, zh::reply& reply);

	std::string get_hashed_password(const std::string& username, const std::string& realm)
	{
		std::string result;

		if (username == m_admin and realm == kRealm)
			result = m_pw;

		return result;
	}

  private:
	my_rest_controller*	m_rest_controller;
	std::string m_admin, m_pw;
};

const std::string my_server::kRealm("PDB-REDO Session Management");

void my_server::welcome(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	// auto v = m_rest_controller->get_all_opnames();
	// json opnames;
	// zeep::to_element(opnames, v);
	// sub.put("opnames", opnames);

	// auto u = m_rest_controller->get_tellers();
	// json tellers;
	// zeep::to_element(tellers, u);
	// sub.put("tellers", tellers);

	create_reply_from_template("index.html", sub, reply);
}

void my_server::admin(const zh::request& request, const zh::scope& scope, zh::reply& reply)
{
	zh::scope sub(scope);

	auto v = m_rest_controller->get_all_sessions();
	json sessions;
	zeep::to_element(sessions, v);
	sub.put("sessions", sessions);

	create_reply_from_template("admin.html", sub, reply);
}

// void my_server::invoer(const zh::request& request, const zh::scope& scope, zh::reply& reply)
// {
// 	zh::scope sub(scope);

// 	sub.put("page", "invoer");

// 	Opname o;

// 	string id = request.get_parameter("id", "");
// 	if (id.empty())
// 	{
// 		o = m_rest_controller->get_last_opname();
// 		o.id.clear();
// 		o.datum.clear();
// 	}
// 	else
// 		o = m_rest_controller->get_opname(id);

// 	json opname;
// 	zeep::to_element(opname, o);
// 	sub.put("opname", opname);

// 	auto u = m_rest_controller->get_tellers();
// 	json tellers;
// 	zeep::to_element(tellers, u);
// 	sub.put("tellers", tellers);

// 	create_reply_from_template("invoer.html", sub, reply);
// }

// void my_server::grafiek(const zh::request& request, const zh::scope& scope, zh::reply& reply)
// {
// 	zh::scope sub(scope);

// 	sub.put("page", "grafiek");

// 	auto v = m_rest_controller->get_all_opnames();
// 	json opnames;
// 	zeep::to_element(opnames, v);
// 	sub.put("opnames", opnames);

// 	auto u = m_rest_controller->get_tellers();
// 	json tellers;
// 	zeep::to_element(tellers, u);
// 	sub.put("tellers", tellers);

// 	create_reply_from_template("grafiek.html", sub, reply);
// }

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
		("address",			po::value<std::string>(),	"External address, default is 0.0.0.0")
		("port",			po::value<uint16_t>(),		"Port to listen to, default is 10339")
		("user,u",			po::value<std::string>(),	"User to run the daemon")
		("db-host",			po::value<std::string>(),	"Database host")
		("db-port",			po::value<std::string>(),	"Database port")
		("db-dbname",		po::value<std::string>(),	"Database name")
		("db-user",			po::value<std::string>(),	"Database user name")
		("db-password",		po::value<std::string>(),	"Database password")
		("admin-user",		po::value<std::string>(),	"Administrator user name")
		("admin-password",	po::value<std::string>(),	"Administrator password");

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

		std::string admin = vm["admin-user"].as<std::string>();
		std::string pwhash = zh::md5(admin + ':' + my_server::kRealm + ':' + vm["admin-password"].as<std::string>()).finalise();

		zh::daemon server([cs=ba::join(vConn, " "), admin, pwhash]()
		{
			return new my_server(cs, admin, pwhash);
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