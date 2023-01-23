/*
   Created by: Maarten L. Hekkelman
   Date: woensdag 09 januari, 2019
*/

#include "rama-angles.hpp"
#include "ramachandran.hpp"
#include "revision.hpp"

#include <zeep/http/daemon.hpp>
#include <zeep/http/html-controller.hpp>
#include <zeep/json/element.hpp>
#include <zeep/json/parser.hpp>

#include <mcfp.hpp>

#include <cif++.hpp>

#include <filesystem>
#include <thread>
#include <future>

namespace fs = std::filesystem;
namespace zh = zeep::http;

using json = zeep::json::element;

// --------------------------------------------------------------------

struct ResidueInfo
{
	ResidueInfo(const ResidueInfo &) = default;
	ResidueInfo &operator=(const ResidueInfo &) = default;

	ResidueInfo(const std::string &compound_id, int seq_id, const std::string &auth_seq_id, const std::optional<std::string> &iCode)
		: compound_id(compound_id)
		, seq_id(seq_id)
		, auth_seq_id(auth_seq_id)
		, pdb_ins_code(iCode)
	{
	}

	ResidueInfo(const std::string &compound_id, int seq_id, const std::string &auth_seq_id, const std::optional<std::string> &iCode, float psi, float phi, bool cis = false)
		: compound_id(compound_id)
		, seq_id(seq_id)
		, auth_seq_id(auth_seq_id)
		, pdb_ins_code(iCode)
		, psi(psi)
		, phi(phi)
		, cis(cis)
	{
	}

	std::string compound_id;
	int seq_id;
	std::string auth_seq_id;
	std::optional<std::string> pdb_ins_code;
	float psi = 360, phi = 360;
	std::string rama;

	//	std::string	rota;
	//	std::string	cis_peptide;

	bool cis = false;

	json toJSON() const;
};

json ResidueInfo::toJSON() const
{
	json result = {
		{ "seq_id", seq_id },
		{ "auth_seq_id", auth_seq_id },
		{ "compound_id", compound_id },
	};

	if (pdb_ins_code.has_value())
		result["pdbx_PDB_ins_code"] = *pdb_ins_code;

	if (phi < 360 and psi < 360)
	{
		result["phi"] = phi;
		result["psi"] = psi;
		result["rama"] = rama;
	}

	if (cis)
		result["cis"] = "y";

	return result;
}

// --------------------------------------------------------------------

struct ChainInfo
{
	ChainInfo(const ChainInfo &) = default;
	ChainInfo &operator=(const ChainInfo &) = default;

	ChainInfo(const std::string &entityID, int modelID, const std::string &authAsymID, const std::string &asymID)
		: entity_id(entityID)
		, model_id(modelID)
		, asym_id(asymID)
		, auth_asym_id(authAsymID)
	{
	}

	std::string entity_id;
	int model_id;
	std::string asym_id, auth_asym_id;
	std::vector<ResidueInfo> residues;
};

std::vector<ChainInfo> CalculateChainInfo(cif::mm::structure &structure)
{
	std::vector<ChainInfo> result;

	for (auto &poly : structure.polymers())
	{
		std::string asymID = poly.get_asym_id();

		result.emplace_back(poly.get_entity_id(), 1, poly.get_auth_asym_id(), asymID);
		auto &residues = result.back().residues;

		for (auto &r : poly)
		{
			std::optional<std::string> iCode;
			if (not r.get_pdb_ins_code().empty())
				iCode = r.get_pdb_ins_code();

			std::auth_seq_id = 

			residues.emplace_back(r.get_compound_id(), r.get_seq_id(), r.get_auth_seq_id(), iCode, r.psi(), r.phi(), r.is_cis());
		}

		for (size_t i = 1; i + 1 < residues.size(); ++i)
		{
			auto &ri = residues[i];

			bool prePro = (i + 1 < residues.size() and residues[i + 1].compound_id == "PRO" and residues[i].compound_id != "PRO");

			switch (calculateRamachandranScore(ri.compound_id, prePro, ri.phi, ri.psi))
			{
				case rsNotAllowed: ri.rama = "OUTLIER"; break;
				case rsAllowed: ri.rama = "Allowed"; break;
				case rsFavoured: ri.rama = "Favored"; break;
			}
		}
	}

	return result;
}

// --------------------------------------------------------------------

json CreateJSONForStructureFile(const std::string &inFile)
{
	if (cif::VERBOSE)
		std::cerr << "Reading file " << inFile << std::endl;

	cif::file f = cif::pdb::read(inFile);

	if (cif::VERBOSE)
		std::cerr << "Creating structure" << std::endl;

	cif::mm::structure structure(f);
	std::string id = f.front().name();
	cif::to_lower(id);

	// --------------------------------------------------------------------

	auto ciList = CalculateChainInfo(structure);
	std::set<std::string> entities;
	for (auto &ci : ciList)
		entities.insert(ci.entity_id);

	// --------------------------------------------------------------------

	json molecules;

	for (std::string entityID : entities)
	{
		json chains;

		for (auto &ci : ciList)
		{
			if (ci.entity_id != entityID)
				continue;

			json residues;

			for (auto &ri : ci.residues)
				residues.emplace_back(ri.toJSON());

			json chain = {
				{ "models", { { { "model_id", 1 },
								{ "residues", std::move(residues) } } } },
				{ "auth_asym_id", ci.auth_asym_id },
				{ "asym_id", ci.asym_id }
			};

			chains.emplace_back(std::move(chain));
		}

		json molecule = {
			{ "entity_id", entityID },
			{ "chains", std::move(chains) }
		};

		molecules.push_back(std::move(molecule));
	}

	json result = {
		{ id, { { "molecules", std::move(molecules) } } }
	};

	return result;
}

std::vector<ChainInfo> CreateChainInfo(fs::path inFile)
{
	if (cif::VERBOSE)
		std::cerr << "Reading file " << inFile << std::endl;

	cif::file f = cif::pdb::read(inFile);

	if (cif::VERBOSE)
		std::cerr << "Creating structure" << std::endl;

	cif::mm::structure structure(f);

	return CalculateChainInfo(structure);
}

json CreateJSONForStructureFiles(const std::string &inOldFile, const std::string &inNewFile)
{
	auto f1 = std::async(std::launch::async, [file=inOldFile]() { return CreateChainInfo(file); });
	auto f2 = std::async(std::launch::async, [file=inNewFile]() { return CreateChainInfo(file); });

	f1.wait();
	f2.wait();

	auto ciListOld = f1.get();
	auto ciListNew = f2.get();

	// --------------------------------------------------------------------
	
	std::set<std::string> entities;
	for (auto &ci : ciListOld)
		entities.insert(ci.entity_id);
	for (auto &ci : ciListNew)
		entities.insert(ci.entity_id);

	// --------------------------------------------------------------------

	json molecules;

	for (std::string entityID : entities)
	{
		json chains;

		auto ci_o = std::find_if(ciListOld.begin(), ciListOld.end(), [entityID](const ChainInfo &ci) { return ci.entity_id == entityID; });
		auto ci_n = std::find_if(ciListNew.begin(), ciListNew.end(), [entityID](const ChainInfo &ci) { return ci.entity_id == entityID; });

		while (ci_o != ciListOld.end() and ci_o->entity_id == entityID and
			ci_n != ciListNew.end() and ci_n->entity_id == entityID)
		{
			json residues;

			auto put_res = [&residues](const ResidueInfo &ri) -> json&
			{
				auto &res = residues.emplace_back(json{
					{ "seq_id", ri.seq_id },
					{ "auth_seq_id", ri.auth_seq_id },
					{ "compound_id", ri.compound_id }
				});

				if (ri.pdb_ins_code.has_value())
					res["pdbx_PDB_ins_code"] = *ri.pdb_ins_code;
				
				return res;
			};

			auto put_data = [](json &d, const ResidueInfo &ri)
			{
				if (ri.phi < 360)
					d["phi"] = ri.phi;
				if (ri.psi < 360)
					d["psi"] = ri.psi;
				if (ri.phi < 360 and ri.psi < 360)
					d["rama"] = ri.rama;
				if (ri.cis)
					d["cis"] = "y";
			};

			auto ri_o = ci_o->residues.begin();
			auto ri_n = ci_n->residues.begin();

			while (ri_o != ci_o->residues.end() and ri_n != ci_n->residues.end())
			{
				if (ri_o->seq_id != ri_n->seq_id)
				{
					if (ri_o->seq_id < ri_n->seq_id)
					{
						auto &res = put_res(*ri_o);
						put_data(res["orig"], *ri_o);
						++ri_o;
					}
					else
					{
						auto &res = put_res(*ri_n);
						put_data(res["redo"], *ri_n);
						++ri_n;
					}
					continue;
				}

				if (ri_o->auth_seq_id != ri_n->auth_seq_id or
					ri_o->compound_id != ri_n->compound_id)
				{
					throw std::runtime_error("Inconsistent data in old vs. new");
				}

				auto &res = put_res(*ri_o);
				put_data(res["orig"], *ri_o);
				put_data(res["redo"], *ri_n);

				++ri_o;
				++ri_n;
			}

			json chain = {
				{ "models", { { { "model_id", 1 },
								{ "residues", std::move(residues) } } } },
				{ "auth_asym_id", ci_o->auth_asym_id },
				{ "asym_id", ci_o->asym_id }
			};

			chains.emplace_back(std::move(chain));

			++ci_o;
			++ci_n;
		}

		json molecule = {
			{ "entity_id", entityID },
			{ "chains", std::move(chains) }
		};

		molecules.push_back(std::move(molecule));
	}

	return molecules;
}

#if BUILD_WEB_SERVICE
// --------------------------------------------------------------------

class RamaAnglesHtmlController : public zh::html_controller
{
  public:
	RamaAnglesHtmlController()
		: zh::html_controller()
	{
		mount("{,index,index.html}", &RamaAnglesHtmlController::handle_welcome);
		mount("{orig,redo}/", &RamaAnglesHtmlController::handle_rama_request);
		mount("{css,scripts,fonts,images}/", &RamaAnglesHtmlController::handle_file);
	}

	void handle_welcome(const zh::request &request, const zh::scope &scope, zh::reply &reply);
	void handle_rama_request(const zh::request &request, const zh::scope &scope, zh::reply &reply);
};

// --------------------------------------------------------------------

void RamaAnglesHtmlController::handle_welcome(const zh::request &request, const zh::scope &scope, zh::reply &reply)
{
	zh::scope sub(scope);

	if (request.has_parameter("pdb-id"))
		sub.put("pdbID", request.get_parameter("pdb-id"));

#if DEBUG
	sub.put("url", "http://localhost:10336/");
#endif

	get_template_processor().create_reply_from_template("index.html", sub, reply);
}

void RamaAnglesHtmlController::handle_rama_request(const zh::request &request, const zh::scope &scope, zh::reply &reply)
{
	fs::path file = scope["baseuri"].as<std::string>();

	std::string selector = file.parent_path().string();
	std::string pdbid = file.filename().string();

	ba::to_lower(pdbid);

	if (pdbid.length() != 4) // sja... TODO!!! fix
		throw std::runtime_error("not a valid pdbid");

	fs::path f;
	if (selector == "orig")
		f = gPDBDIR / pdbid.substr(1, 2) / (pdbid + ".cif.gz");
	else if (selector == "redo")
		f = gPDBREDODIR / pdbid.substr(1, 2) / pdbid / (pdbid + "_final.cif");

	if (not fs::exists(f))
	{
		json result{
			{ "error", "pdb file not found: " + f.string() }
		};

		reply.set_content(result);
		reply.set_status(zh::not_found);
		return;
	}

	json result = CreateJSONForStructureFile(f.string());

	// reply.set_header("Access-Control-Allow-Origin", "*");

	// add the rama score from data.json
	f = gPDBREDODIR / pdbid.substr(1, 2) / pdbid / "data.json";
	if (fs::exists(f))
	{
		try
		{
			std::ifstream dataFile(f);
			json data;
			zeep::json::parse_json(dataFile, data);

			result[pdbid]["zscore"] = data[selector == "orig" ? "OZRAMA" : "FZRAMA"].as<float>();
		}
		catch (const std::exception &ex)
		{
			std::cerr << "Error parsing data.json: " << ex.what() << std::endl;
		}
	}

	reply.set_content(result);
}

// --------------------------------------------------------------------

int main_server(int argc, const char *argv[])
{
	using namespace std::literals;

	int result = 0;

	po::options_description visible(argv[0] + " [options] command"s);
	visible.add_options()("help,h", "Display help message")("verbose,v", "Verbose output");

	po::options_description config = get_config_options();

	po::options_description hidden("hidden options");
	hidden.add_options()("command", po::value<std::string>(), "Command, one of start, stop, status or reload")("debug,d", po::value<int>(), "Debug level (for even more verbose output)");

	po::options_description cmdline_options;
	cmdline_options.add(visible).add(config).add(hidden);

	po::positional_options_description p;
	p.add("command", 1);

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);

	fs::path configFile = "rama-angles.conf";
	if (not fs::exists(configFile) and getenv("HOME") != nullptr)
		configFile = fs::path(getenv("HOME")) / ".config" / "rama-angles.conf";

	if (vm.count("config") != 0)
	{
		configFile = vm["config"].as<std::string>();
		if (not fs::exists(configFile))
			throw std::runtime_error("Specified config file does not seem to exist");
	}

	if (fs::exists(configFile))
	{
		po::options_description config_options;
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

	char exePath[PATH_MAX + 1];
	int r = readlink("/proc/self/exe", exePath, PATH_MAX);
	if (r > 0)
	{
		exePath[r] = 0;
		gExePath = exePath;
		// gExePath = fs::weakly_canonical(gExePath);
	}

	if (not fs::exists(gExePath))
		gExePath = fs::weakly_canonical(argv[0]);

	std::string docroot = "docroot";
#if not USE_RSRC
	if (vm.count("docroot"))
		docroot = vm["docroot"].as<std::string>();

	if (not fs::is_directory(docroot))
	{
		std::cerr << "Docroot directory " << docroot << " does not exist" << std::endl;
		exit(1);
	}
#endif

	if (vm.count("pdb-dir"))
		gPDBDIR = vm["pdb-dir"].as<std::string>();

	if (vm.count("pdb-redo-dir"))
		gPDBREDODIR = vm["pdb-redo-dir"].as<std::string>();

	const char *ccp4 = getenv("CCP4");
	std::string ccp4Dir = ccp4 ? ccp4 : "";
	if (vm.count("ccp4-dir"))
		ccp4Dir = vm["ccp4-dir"].as<std::string>();
	if (not ccp4Dir.empty())
	{
		auto clibdMon = fs::path(ccp4Dir) / "lib" / "data" / "monomers";
		setenv("CLIBD_MON", clibdMon.string().c_str(), 1);
	}

	// --------------------------------------------------------------------

	zh::daemon server([&]()
		{
		auto s = new zeep::http::server(docroot);
		s->add_controller(new RamaAnglesHtmlController());
		return s; },
		"rama-angles");

	std::string user = "www-data";
	if (vm.count("user") != 0)
		user = vm["user"].as<std::string>();

	std::string address = "0.0.0.0";
	if (vm.count("address"))
		address = vm["address"].as<std::string>();

	uint16_t port = 10336;
	if (vm.count("port"))
		port = vm["port"].as<uint16_t>();

	std::string command = vm["command"].as<std::string>();

	if (command == "start")
	{
		std::cout << "starting server at " << address << ':' << port << std::endl;

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

	return result;
}
#endif

// --------------------------------------------------------------------

fs::path make_path(const fs::path &dir, std::string file, const std::string &id)
{
	for (auto i = file.find("${id}"); i != std::string::npos; i = file.find("${id}", i))
	{
		file.replace(i, 5, id);
		i += id.length() - 5;
	}

	fs::path result = dir / file;
	std::error_code ec;

	if (not fs::exists(result, ec))
	{
		std::cerr << "Input file does not exist: " << result.string() << std::endl;
		exit(1);
	}

	return result;
}

void add_rama_angles(zeep::json::element &data, const std::filesystem::path dir)
{
	auto &config = mcfp::config::instance();

	// --------------------------------------------------------------------
	
	const char *ccp4 = getenv("CCP4");
	std::string ccp4Dir = ccp4 ? ccp4 : "";
	if (config.has("ccp4-dir"))
		ccp4Dir = config.get<std::string>("ccp4-dir");
	if (not ccp4Dir.empty())
	{
		auto clibdMon = fs::path(ccp4Dir) / "lib" / "data" / "monomers";
		setenv("CLIBD_MON", clibdMon.string().c_str(), 1);
	}

	// --------------------------------------------------------------------

	std::string id;
	if (config.operands().size() > 1)
		id = config.operands()[1];
	else
		id = data["pdbid"].as<std::string>();

	fs::path old_xyzin = make_path(dir, config.get<std::string>("original-file-pattern"), id);
	fs::path new_xyzin = make_path(dir, config.get<std::string>("final-file-pattern"), id);

	data["rama-angles"] = CreateJSONForStructureFiles(old_xyzin, new_xyzin);	
}

#if 0
int main_calculate(int argc, const char *argv[])
{
	using namespace std::literals;

	auto &config = mcfp::config::instance();

	config.init(
		"usage: rama-angles [options] <pdb-redo-entry-dir> [pdb-redo-entry-id]",
		mcfp::make_option("help,h", "Display this help message"),
		mcfp::make_option("version", "Show version information"),
		mcfp::make_option("verbose,v", "Be more verbose"),
		mcfp::make_option<std::string>("config", "Path to a config file"),

		mcfp::make_option<std::string>("ccp4-dir", "CCP4 directory, if not specified the environmental variable CCP4 will be used (and should be available)"),

		// mcfp::make_option<std::string>("data-json-file-pattern", "data.json", "Pattern for the data.json file"),
		mcfp::make_option<std::string>("original-file-pattern", "${id}_0cyc.pdb.gz", "Pattern for the original xyzin file"),
		mcfp::make_option<std::string>("final-file-pattern", "${id}_final.cif", "Pattern for the final xyzin file")
	);

	std::error_code ec;
	config.parse(argc, argv, ec);
	if (ec)
	{
		std::cerr << "Error parsing arguments: " << ec.message() << std::endl;
		exit(1);
	}

	if (config.has("help") or config.operands().empty())
	{
		std::cout << config << std::endl;
		exit(config.has("help") ? 0 : 1);
	}

	config.parse_config_file("config", kProjectName + ".conf"s, { "/etc" }, ec);
	if (ec)
	{
		std::cerr << "Error parsing config file: " << ec.message() << std::endl;
		exit(1);
	}

	if (config.has("version"))
	{
		write_version_string(std::cout, config.has("verbose"));
		exit(0);
	}

	cif::VERBOSE = config.count("verbose");

	// --------------------------------------------------------------------
	
	const char *ccp4 = getenv("CCP4");
	std::string ccp4Dir = ccp4 ? ccp4 : "";
	if (config.has("ccp4-dir"))
		ccp4Dir = config.get<std::string>("ccp4-dir");
	if (not ccp4Dir.empty())
	{
		auto clibdMon = fs::path(ccp4Dir) / "lib" / "data" / "monomers";
		setenv("CLIBD_MON", clibdMon.string().c_str(), 1);
	}

	// -------------------------------------------------------------------
	// Input files

	fs::path dir = config.operands().front();
	if (not fs::is_directory(dir, ec))
	{
		std::cerr << "Not a directory: " << dir.string() << std::endl;
		exit(1);
	}

	// --------------------------------------------------------------------
	
	json data;
	fs::path data_json = dir / "data.json";
	std::ifstream data_in(data_json);

	if (not data_in.is_open())
	{
		std::cerr << "Could not open data.json file" << std::endl;
		exit(1);
	}

	zeep::json::parse_json(data_in, data);
	data_in.close();

	// --------------------------------------------------------------------

	std::string id;
	if (config.operands().size() > 1)
		id = config.operands()[1];
	else
		id = data["pdbid"].as<std::string>();

	fs::path old_xyzin = make_path(dir, config.get<std::string>("original-file-pattern"), id);
	fs::path new_xyzin = make_path(dir, config.get<std::string>("final-file-pattern"), id);

	data["rama-angles"] = CreateJSONForStructureFiles(old_xyzin, new_xyzin);

	fs::path tmp_file = data_json;
	tmp_file.replace_extension(".ra-new");
	std::ofstream out(tmp_file);
	if (not out.is_open())
	{
		std::cerr << "Could not open output file" << std::endl;
		exit(1);
	}
	out << data;
	out.close();

	auto backup = data_json;
	fs::rename(data_json, backup.replace_extension(".ra-old"), ec);
	if (not ec)
		fs::rename(tmp_file, data_json, ec);
	if (not ec)
		fs::remove(backup, ec);

	if (ec)
	{
		std::cerr << "Something went wrong renaming files: " << ec.message() << std::endl;
		exit(1);
	}

	return 0;
}

// --------------------------------------------------------------------

int main(int argc, const char *argv[])
{
	int result = 0;

	try
	{
		result = main_calculate(argc, argv);
	}
	catch (const std::exception &ex)
	{
		std::cerr << ex.what() << std::endl;
		exit(1);
	}

	// std::string command;

	// if (argc > 1)
	// 	command = argv[1];

	// try
	// {
	// 	// cif::rsrc_loader::init({ { cif::rsrc_loader_type::mrsrc, "", { gResourceIndex, gResourceData, gResourceName } },
	// 	// 	{ cif::rsrc_loader_type::file, "." } });

	// 	// if (command == "server")
	// 	// 	result = main_server(argc - 1, argv + 1);
	// 	// else
	// 	 if (command == "calculate")
	// 		result = main_calculate(argc - 1, argv + 1);
	// 	else
	// 	{
	// 		std::cerr << "Usage: mini-ibs command [options]" << std::endl
	// 				  << "  where command is one of" << std::endl
	// 				  << "  server     to start/stop a web server" << std::endl
	// 				  << "  calculate  to calculate the rama angles for a file" << std::endl
	// 				  << std::endl;
	// 		exit(1);
	// 	}
	// }
	// catch (const std::exception &ex)
	// {
	// 	std::cerr << "exception:" << std::endl
	// 			  << ex.what() << std::endl;
	// 	result = 1;
	// }

	return result;
}

#endif