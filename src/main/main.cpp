#include <shared/scheduler.h>
#include <simpleServer/abstractService.h>
#include <shared/stdLogFile.h>
#include <shared/default_app.h>
#include <algorithm>
#include <iostream>

#include "../server/src/simpleServer/abstractStream.h"
#include "../server/src/simpleServer/address.h"
#include "../server/src/simpleServer/http_filemapper.h"
#include "../server/src/simpleServer/http_pathmapper.h"
#include "../server/src/simpleServer/http_server.h"
#include "../shared/linux_crash_handler.h"

#include "shared/ini_config.h"
#include "shared/shared_function.h"
#include "shared/cmdline.h"
#include "shared/future.h"
#include "../shared/sch2wrk.h"
#include "abstractExtern.h"
#include "istockapi.h"
#include "istatsvc.h"
#include "ordergen.h"
#include "mtrader.h"
#include "report.h"
#include "spread_calc.h"
#include "ext_stockapi.h"
#include "authmapper.h"
#include "webcfg.h"
#include "spawn.h"
#include "stats2report.h"
#include "backtest.h"
#include "traders.h"

using ondra_shared::StdLogFile;
using ondra_shared::StrViewA;
using ondra_shared::LogLevel;
using ondra_shared::logNote;
using ondra_shared::logInfo;
using ondra_shared::logProgress;
using ondra_shared::logError;
using ondra_shared::logFatal;
using ondra_shared::logDebug;
using ondra_shared::LogObject;
using ondra_shared::shared_function;
using ondra_shared::parseCmdLine;
using ondra_shared::Scheduler;
using ondra_shared::Worker;
using ondra_shared::Dispatcher;
using ondra_shared::RefCntObj;
using ondra_shared::RefCntPtr;
using ondra_shared::schedulerGetWorker;

std::unique_ptr<Traders> traders;

template<typename Fn>
auto run_in_worker(Worker wrk, Fn &&fn) -> decltype(fn()) {
	using Ret = decltype(fn());
	ondra_shared::Countdown c(1);
	std::exception_ptr exp;
	std::optional<Ret> ret;
	wrk >> [&] {
		try {
			ret = fn();
		} catch (...) {
			exp = std::current_exception();
		}
		c.dec();
	};
	c.wait();
	if (exp != nullptr) {
		std::rethrow_exception(exp);
	}
	return *ret;
}

static int eraseTradeHandler(Worker &wrk, simpleServer::ArgList args, simpleServer::Stream stream, bool trunc) {
	if (args.length<2) {
		stream << "Needsd arguments: <trader_ident> <trade_id>\n";
		return 1;
	} else {
		NamedMTrader *trader = traders->find(args[0]);
		if (trader == nullptr) {
			stream << "Trader idenitification is invalid: " << args[0] << "\n";
			return 2;
		} else {
			try {
				bool res = run_in_worker(wrk, [&] {
					return trader->eraseTrade(args[1],trunc);
				});
				if (!res) {
					stream << "Trade not found: " << args[1] << "\n";
					return 2;
				} else {
					stream << "OK\n";
					return 0;
				}
			} catch (std::exception &e) {
				stream << e.what() << "\n";
				return 3;
			}
		}
	}
}

static int cmd_singlecmd(Worker &wrk, simpleServer::ArgList args, simpleServer::Stream stream, void (MTrader::*fn)()) {
	if (args.empty()) {
		stream << "Need argument: <trader_ident>\n"; return 1;
	}
	NamedMTrader *trader = traders->find(args[0]);
	if (trader == nullptr) {
		stream << "Trader idenitification is invalid: " << args[0] << "\n";
		return 1;
	}
	try {
		run_in_worker(wrk, [&]{
			(trader->*fn)();return true;
		});
		stream << "OK\n";
		return 0;
	} catch (std::exception &e) {
		stream << e.what() << "\n";
		return 3;
	}
}



static int cmd_achieve(Worker &wrk, simpleServer::ArgList args, simpleServer::Stream stream) {
	if (args.length != 3) {
		stream << "Need arguments: <trader_ident> <price> <balance>\n"; return 1;
	}

	double price = strtod(args[1].data,nullptr);
	double balance = strtod(args[2].data,nullptr);
	if (price<=0) {
		stream << "second argument must be positive real numbers. Use dot (.) as decimal point\n";return 1;
	}

	NamedMTrader *trader = traders->find(args[0]);
	if (trader == nullptr) {
		stream << "Trader idenitification is invalid: " << args[0] << "\n";
		return 1;
	}
	try {
		run_in_worker(wrk, [&]{
			trader->achieve_balance(price,balance);return true;
		});
		stream << "OK\n";
		return 0;
	} catch (std::exception &e) {
		stream << e.what() << "\n";
		return 3;
	}
}

static int cmd_backtest(Worker &wrk, simpleServer::ArgList args, simpleServer::Stream stream, const std::string &cfgfname, IStockSelector &stockSel, Report &rpt) {
	if (args.length < 1) {
		stream << "Need arguments: <trader_ident> [option=value ...]\n"; return 1;
	}
	NamedMTrader *trader = traders->find(args[0]);
	if (trader == nullptr) {
		stream << "Trader idenitification is invalid: " << args[0] << "\n";
		return 1;
	}

	stream << "Preparing chart\n";
	stream.flush();
	NamedMTrader &t = *trader;
	try {
		std::vector<ondra_shared::IniItem> options;
		for (std::size_t i = 1; i < args.length; i++) {
			auto arg = args[i];
			auto splt = arg.split("=",2);
			StrViewA key = splt();
			StrViewA value = splt();
			key = key.trim(isspace);
			value = value.trim(isspace);
			options.emplace_back(ondra_shared::IniItem::data, args[0], key, value);
		}

		std::optional<BacktestControl> backtest;
		BacktestControl::BtReport *btrpt_cntr;
		run_in_worker(wrk, [&] {
			auto cfg = BacktestControl::loadConfig(cfgfname, args[0], options,t.getLastSpread());
			auto btrpt = std::make_unique<BacktestControl::BtReport>(
					std::make_unique<Stats2Report>(
							[=](CalcSpreadFn &&fn) {fn();},
							"backtest",
							rpt,
							cfg.calc_spread_minutes));
			btrpt_cntr = btrpt.get();
			t.init();
			backtest.emplace(stockSel, std::move(btrpt), cfg, t.getChart(),  t.getInternalBalance());
			return true;
		});

		stream << "Running ('.' - per hour, '+' - report)\n";

		std::mutex wrlock;
		auto wrout = [&](StrViewA x) {
			std::lock_guard<std::mutex> _(wrlock);
			stream << x;
			return stream.flush();
		};

		Scheduler sch = Scheduler::create();
		sch.each(std::chrono::seconds(1)) >> [p = 0,&wrout]() mutable {
			char c[2];
			p = (p + 1) % 4;
			c[1] = '\b';
			c[0] = "\\|/-"[p];
			wrout(StrViewA(c,2));
		};

		int mdv = 0;
		auto tc = std::chrono::system_clock::now();
		while (backtest->step()) {
			auto tn = std::chrono::system_clock::now();
			mdv++;
			if (mdv >= 60) {
				if (!wrout(".")) break;
				mdv = 0;
			}
			if (std::chrono::duration_cast<std::chrono::seconds>(tn-tc).count()>50) {
				run_in_worker(wrk,[&] {
					btrpt_cntr->flush();
					rpt.genReport();
					wrout("+");
					return true;
				});
				tc = tn;
			}
		}
		sch.clear();
		stream << "\nGenerating report\n";
		stream.flush();
		run_in_worker(wrk,[&] {
			btrpt_cntr->flush();
			rpt.genReport();
			return true;
		});
		stream << "Done\n";
		return 0;
	} catch (std::exception &e) {
		stream << e.what() << "\n";
		return 2;
	}

}

static int cmd_config(Worker &wrk, simpleServer::ArgList args, simpleServer::Stream stream, const ondra_shared::IniConfig &cfg) {
	if (args.length < 1) {
		stream << "Need argument: <trader_ident>\n"; return 1;
	}
	auto sect = cfg[args[0]];
	std::stringstream buff;
	MTrader::showConfig(sect, false, buff);
	std::vector<std::string> list;
	buff.seekp(0);
	std::string line;
	while (std::getline(buff,line)) list.push_back(line);
	std::sort(list.begin(),list.end());
	for (auto &&k : list)
		stream << k << "\n";
	return 0;
}


static ondra_shared::CrashHandler report_crash([](const char *line) {
	ondra_shared::logFatal("CrashReport: $1", line);
});


class App: public ondra_shared::DefaultApp {
public:

	App(): ondra_shared::DefaultApp({
		App::Switch{'t',"dry_run",[this](auto &&){this->test = true;},"dry run"},
	},std::cout) {}

	bool test = false;

	virtual void showHelp(const std::initializer_list<Switch> &defsw) {
		const char *commands[] = {
				"",
				"Commands",
				"",
				"start        - start service on background",
			    "stop         - stop service ",
				"restart      - restart service ",
			    "run          - start service at foreground",
				"status       - print status",
				"pidof        - print pid",
				"wait         - wait until service exits",
				"logrotate    - close and reopen logfile",
				"calc_range   - calculate and print trading range for each pair",
				"get_all_pairs- print all tradable pairs - need broker name as argument",
				"erase_trade  - erases trade. Need id of trader and id of trade",
				"reset        - erases all trades expect the last one",
				"achieve      - achieve an internal state (achieve mode)",
				"repair       - repair pair",
				"backtest     - backtest",
				"show_config  - shows trader's complete configuration"
		};

		const char *intro[] = {
				"Copyright (c) 2019 Ondrej Novak. All rights reserved.",
				"",
				"This work is licensed under the terms of the MIT license.",
				"For a copy, see <https://opensource.org/licenses/MIT>",
				"",
				"Usage: mmbot [...switches...] <cmd> [<args...>]",
				""
		};

		for (const char *c : intro) wordwrap(c);
		ondra_shared::DefaultApp::showHelp(defsw);
		for (const char *c : commands) wordwrap(c);
	}

	auto createRestartFn() {
		return [this] {
			std::string switches;
			switches.push_back('-');
			if (test) switches.push_back('t');
			if (debug) switches.push_back('d');
			switches.push_back('f');
			std::string appPath = this->appPath.string();
			std::string configPath = this->configPath.string();
			const char *args[] = {
					appPath.c_str(),
					switches.c_str(),
					configPath.c_str(),
					"restart",
					nullptr
			};
			spawn(appPath.c_str(), args);
		};
	}
};



int main(int argc, char **argv) {

	try {

		App app;

		if (!app.init(argc, argv)) {
			std::cerr << "Invalid parameters at:" << app.args->getNext() << std::endl;
			return 1;
		}

		if (!!*app.args) {
			try {
				StrViewA cmd = app.args->getNext();

				auto servicesection = app.config["service"];
				auto pidfile = servicesection.mandatory["inst_file"].getPath();
				auto name = servicesection["name"].getString("mmbot");
				auto user = servicesection["user"].getString();

				std::vector<StrViewA> argList;
				while (!!*app.args) argList.push_back(app.args->getNext());

				report_crash.install();


				return simpleServer::ServiceControl::create(name, pidfile, cmd,
					[&](simpleServer::ServiceControl cntr, ondra_shared::StrViewA name, simpleServer::ArgList arglist) {

					{
						if (app.verbose && cntr.isDaemon()) {

							std::cerr << "Verbose is not avaiable in daemon mode" << std::endl;
							return 100;
						}

						if (!user.empty()) {
							cntr.changeUser(user);
						}

						cntr.enableRestart();

						Scheduler sch = ondra_shared::Scheduler::create();

						cntr.addCommand("logrotate",[=](const simpleServer::ArgList &, simpleServer::Stream ) {
							ondra_shared::logRotate();
							return 0;
						});



						auto lstsect = app.config["traders"];
						auto names = lstsect.mandatory["list"].getString();
						auto storagePath = lstsect.mandatory["storage_path"].getPath();
						auto storageBinary = lstsect["storage_binary"].getBool(true);
						auto spreadCalcInterval = lstsect["spread_calc_interval"].getUInt(10);
						auto rptsect = app.config["report"];
						auto rptpath = rptsect.mandatory["path"].getPath();
						auto rptinterval = rptsect["interval"].getUInt(864000000);



						StorageFactory sf(storagePath,5,storageBinary?Storage::binjson:Storage::json);
						StorageFactory rptf(rptpath,2,Storage::json);

						Report rpt(rptf.create("report.json"), rptinterval, false);



						Worker wrk = schedulerGetWorker(sch);

						traders = std::make_unique<Traders>(
								sch,app.config["brokers"], app.test,spreadCalcInterval,sf,rpt
						);

						RefCntPtr<AuthUserList> aul;

						traders->loadTraders(app.config, names);

						auto webadminsect = app.config["web_admin"];
						bool webadmin_enabled = webadminsect["enabled"].getBool(false);
						RefCntPtr<WebCfg::State> webcfgstate;
						if (webadmin_enabled) {
							StrViewA webadmin_auth = webadminsect["auth"].getString();
							webcfgstate = new WebCfg::State(sf.create("web_admin_conf"),new AuthUserList, new AuthUserList);
							webcfgstate->setAdminAuth(webadmin_auth);
							webcfgstate->applyConfig(*traders);
							aul = webcfgstate->users;
						} else {
							aul = new AuthUserList;
							aul->setUsers(aul->decodeMultipleBasicAuth(rptsect["http_auth"].getString()));
						}

						auto web_bind = rptsect["http_bind"];

						std::unique_ptr<simpleServer::MiniHttpServer> srv;


					if (web_bind.defined()) {
						simpleServer::NetAddr addr = simpleServer::NetAddr::create(web_bind.getString(),11223);
						srv = std::make_unique<simpleServer::MiniHttpServer>(addr, 1, 1);


						std::vector<simpleServer::HttpStaticPathMapper::MapRecord> paths;
						paths.push_back(simpleServer::HttpStaticPathMapper::MapRecord{
							"/",AuthMapper(name,aul) >>= simpleServer::HttpFileMapper(std::string(rptpath), "index.html")
						});

						if (webadmin_enabled) {
							paths.push_back({
								"/admin",ondra_shared::shared_function<bool(simpleServer::HTTPRequest, ondra_shared::StrViewA)>(WebCfg(webcfgstate,
										name,
										*traders,
										[=](WebCfg::Action &&a) mutable {sch.immediate() >> std::move(a);}))
							});
						}

						(*srv)  >>=  simpleServer::HttpStaticPathMapperHandler(paths);
					}


						logNote("---- Starting service ----");

						cntr.addCommand("calc_range",[&](const simpleServer::ArgList &args, simpleServer::Stream out){

							run_in_worker(wrk,[&] {
								try {
									for(auto &&t:traders->traders) {							;
										std::ostringstream buff;
										auto result = t.second->calc_min_max_range();
										auto ass = t.second->getMarketInfo().asset_symbol;
										auto curs = t.second->getMarketInfo().currency_symbol;
										buff << "Trader " << t.second->getConfig().title
												<< ":" << std::endl
												<< "\tAssets:\t\t\t" << result.assets << " " << ass << std::endl
												<< "\tAssets value:\t\t" << result.value << " " << curs << std::endl
												<< "\tAvailable assets:\t" << result.avail_assets << " " << ass << std::endl
												<< "\tAvailable money:\t" << result.avail_money << " " << curs << std::endl
												<< "\tMin price:\t\t" << result.min_price << " " << curs << std::endl;
										if (result.min_price == 0)
										   buff << "\t - money left:\t\t" << (result.avail_money-result.value) << " " << curs << std::endl;
										buff << "\tMax price:\t\t" << result.max_price << " " << curs << std::endl;
										out << buff.str();
										out.flush();

									}
								} catch (std::exception &e) {
									out << e.what();
								}
								return true;
							});

							return 0;
						});

						cntr.addCommand("get_all_pairs",[&](simpleServer::ArgList args, simpleServer::Stream stream){
							if (args.length < 1) {
								stream << "Append argument: <broker>\n";
								return 1;
							} else {
								StockSelector ss;
								ss.loadStockMarkets(app.config["brokers"], true);
								IStockApi *stock = ss.getStock(args[0]);
								if (stock) {
									for (auto &&k : stock->getAllPairs()) {
										stream << k << "\n";
									}
									return 0;
								} else {
									stream << "Stock is not defined\n";
									return 2;
								}
							}

						});

						cntr.addCommand("erase_trade", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return eraseTradeHandler(wrk, args,stream,false);
						});
						cntr.addCommand("resync_trades_from", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return eraseTradeHandler(wrk, args,stream,true);
						});
						cntr.addCommand("reset", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return cmd_singlecmd(wrk, args,stream,&MTrader::reset);
						});
						cntr.addCommand("achieve", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return cmd_achieve(wrk, args,stream);
						});
						cntr.addCommand("repair", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return cmd_singlecmd(wrk, args,stream,&MTrader::repair);
						});
						cntr.addCommand("backtest", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return cmd_backtest(wrk, args, stream, app.configPath.string(), traders->stockSelector, rpt);
						});
						cntr.addCommand("show_config", [&](simpleServer::ArgList args, simpleServer::Stream stream){
							return cmd_config(wrk, args, stream, app.config);
						});
						std::size_t id = 0;
						cntr.addCommand("run",[&](simpleServer::ArgList, simpleServer::Stream) {

							ondra_shared::PStdLogProviderFactory current =
									&dynamic_cast<ondra_shared::StdLogProviderFactory &>(*ondra_shared::AbstractLogProviderFactory::getInstance());
							ondra_shared::PStdLogProviderFactory logcap = rpt.captureLog(current);

							sch.immediate() >> [logcap]{
								ondra_shared::AbstractLogProvider::getInstance() = logcap->create();
							};


							auto main_cycle = [&] {


								try {
									traders->runTraders();
									rpt.genReport();
								} catch (std::exception &e) {
									logError("Scheduler exception: $1", e.what());
								}
							};

							sch.after(std::chrono::seconds(1)) >> main_cycle;

							id = sch.each(std::chrono::minutes(1)) >> main_cycle;


							return 0;
						});

						cntr.dispatch();

						sch.remove(id);
						sch.sync();
						traders->clear();
					}
					logNote("---- Exit ----");


					return 0;

					}, simpleServer::ArgList(argList.data(), argList.size()),
					cmd == "calc_range" || cmd == "get_all_pairs" || cmd == "achieve" || cmd == "reset" || cmd=="repair" || cmd == "backtest" || cmd == "show_config");
			} catch (std::exception &e) {
				std::cerr << "Error: " << e.what() << std::endl;
				return 2;
			}
		}
	} catch (std::exception &e) {
		std::cerr << "Error:" << e.what() << std::endl;
		return 1;
	}
}
