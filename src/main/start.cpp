
#include <iostream>
#include <string>
#include <fstream>

#include <imtjson/value.h>
#include <imtjson/parser.h>
#include <shared/default_app.h>
#include <userver/netaddr.h>
#include <shared/logOutput.h>

#include "errhandler.h"

#include "httpapi.h"

#include "server.h"

#include "core.h"

#include "istockapi.h"

#include "backtest2.h"

#include "trader_factory.h"
#include "registrations.h"
#include "licence.h"
#include "auth.h"


int main(int argc, char **argv) {

	using namespace ondra_shared;
	using namespace userver;


	json::enableParsePreciseNumbers = true;
	init_registrations();

	std::optional<int> open_port;
	bool suspended = false;
	std::string admin_account;
	std::string usage = str_licence;
	usage.append("\n\nUsage: ");
	usage.append(argv[0]);
	usage.append(" [switches...]");

	DefaultApp theApp({
		{'p',"port",[&](CmdArgIter &x){open_port = x.getUInt();},
			"Open port on localhost allows to access the web interface","<number>"},
		{'a',"admin",[&](CmdArgIter &){admin_account=Auth::generate_secret().substr(0,16);},
			"Create temporary admin account - prints credentials at standard output - if you forgot the password"},
		{'s',"suspended",[&](CmdArgIter &){suspended=true;},
			"Start with suspended trading. The main cycle is not running. There is no way to start the trading from the web console"}
		},


		std::cerr, usage.c_str());

	if (!theApp.init(argc, argv)) {
		std::cerr << "FATAL: Failed to start application - can't read configuration file or command line error" << std::endl;
		return 1;
	}

	if (!admin_account.empty()) {
		std::cout << "Credentials for temporary administration account:" << std::endl
				  << "username: admin" << std::endl
				  << "password: " << admin_account<< std::endl;
	}

	try {
		logNote("-------------- SERVER START -------------------");

		auto bot_core = std::make_unique<BotCore>(theApp.config);
		logProgress("Starting core $1", suspended?"(suspended)":"");
		bot_core->run(suspended);

		NetAddrList listens;
		int http_threads;
		std::size_t max_upload;
		std::string www_path;
		if (open_port.has_value()) {
			auto x = NetAddr::fromString("127.0.0.1", std::to_string(*open_port));
			listens.insert(listens.end(), x.begin(), x.end());
		}
		{
			auto section = theApp.config["server"];
			std::string sock_path = section["socket"].getPath();
			if (!sock_path.empty()) {
				std::string addr = "unix:";
				addr.append(sock_path);
				auto x = NetAddr::fromString(addr,"");
				listens.insert(listens.end(), x.begin(), x.end());
			}
			std::string listen = section["listen"].getString();
			if (!listen.empty()) {
				auto x = NetAddr::fromString(listen,"33801");
				listens.insert(listens.end(), x.begin(), x.end());
			}
			http_threads = section["http_threads"].getUInt(2);
			max_upload = section["upload_limit_kb"].getUInt(10)*1024*1024;
			www_path = section.mandatory["www"].getPath();
		}

		for (const auto &x: listens) {
			logProgress("Listening: $1", x.toString(false));
		}

		AsyncProvider async = createAsyncProvider(1);
		setCurrentAsyncProvider(async);


		auto server = std::make_shared<Server>();

		PHttpAPI api = new HttpAPI(std::move(bot_core), max_upload, www_path);
		api->init(server);

		logProgress("Starting server");
		server->start(listens, http_threads, async);
		server->stopOnSignal();
		logProgress("Send SIGTERM to exit server (pid: $1)", getpid());
		server->addThread();

		logProgress("Stopping server");

		server->stop();



	} catch (...) {
		REPORT_UNHANDLED();
		logFatal("FATAL exception, bailing out ===================");
		return 100;
	}
	logNote("-------------- SERVER EXIT -------------------");
	return 0;



}
#if 0
	if (argc < 3) {
		std::cerr << "Needs definition (json file) and data (json file)";
	}

	std::string definition_fname = argv[1];
	std::string data_fname = argv[2];

	std::ifstream def_file(definition_fname);
	std::ifstream data_file(data_fname);

	json::Value def = json::Value::fromStream(def_file);
	json::Value data = json::Value::fromStream(data_file);


	Trader_Config_Ex traderCfg;
	traderCfg.parse(def["trader"]);
	json::Value minfo_json = def["minfo"];
	json::Value init_json = def["init"];

	IStockApi::MarketInfo minfo;
	minfo = minfo.fromJSON(minfo_json);

	double assets = init_json["assets"].getNumber();
	double currency = init_json["currency"].getNumber();


	std::vector<double> ddata;
	for (json::Value d: data) ddata.push_back(d.getNumber());

	Backtest bt(traderCfg, minfo, assets, currency);

	bt.start(std::move(ddata), 0);

	std::size_t tradeCnt = 0;
	std::cout << "price,buy_price,buy_amount,buy_error,sell_price,sell_amount,sell_error,position,trade_price,trade_size,trade_dir,norm_profit,equilibrium,spread,dynmult_buy,dynmult_sell,budget_total,budget_extra,error" << std::endl;
	while (bt.next()) {
		const auto &tr = bt.get_trades();
		std::size_t e = tr.length;
		if (e == tradeCnt) e++;
		for (std::size_t tidx = tradeCnt; tidx < e; ++tidx) {
			const auto &bo = bt.get_buy_order();
			const auto &so = bt.get_sell_order();
			std::cout << bt.get_cur_price() << ",";
			if (bo.has_value()) std::cout << bo->size << "," << bo->price << ",";
			else std::cout << ",,";
			std::cout << bt.getBuyErr()<< ",";
			if (so.has_value()) std::cout << so->size << "," << so->price << ",";
			else std::cout << ",,";
			std::cout << bt.getSellErr() << ",";
			std::cout << bt.get_position() << ",";
			const auto &misc = bt.get_misc_data();
			if (tidx >= tr.length) {
				//,"trade_price","trade_size","trade_dir","norm_profit",
				std::cout << ",," << misc.trade_dir << ",,";
			} else {
				std::cout << tr[tidx].price << "," << tr[tidx].size << "," << misc.trade_dir << "," << tr[tidx].norm_profit << ",";
			}
			std::cout << misc.equilibrium << ",";
			std::cout << misc.spread<< ",";
			std::cout << misc.dynmult_buy<< ",";
			std::cout << misc.dynmult_sell<< ",";
			std::cout << misc.budget_total<< ",";
			std::cout << misc.budget_extra<< ",";
			std::cout << bt.getGenErr() << std::endl;
		}
		tradeCnt = tr.length;

	}







}
#endif
