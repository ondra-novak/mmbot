#include <shared/scheduler.h>
#include <simpleServer/abstractService.h>
#include <shared/stdLogFile.h>
#include <shared/default_app.h>
#include <algorithm>
#include <iostream>
#include <sstream>

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
#include "mtrader.h"
#include "report.h"
#include "ext_stockapi.h"
#include "authmapper.h"
#include "webcfg.h"
#include "spawn.h"
#include <random>

#include "../imtjson/src/imtjson/binary.h"
#include "../server/src/simpleServer/threadPoolAsync.h"
#include "ext_storage.h"
#include "extdailyperfmod.h"
#include "localdailyperfmod.h"
#include "stats2report.h"
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

ondra_shared::SharedObject<Traders> traders;

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

static int eraseTradeHandler(Worker &wrk, simpleServer::ArgList args, std::ostream &stream, bool trunc) {
	if (args.length<2) {
		stream << "Needsd arguments: <trader_ident> <trade_id>" << std::endl;
		return 1;
	} else {
		auto trader = traders.lock_shared()->find(args[0]);
		if (trader == nullptr) {
			stream << "Trader idenitification is invalid: " << args[0] << std::endl;
			return 2;
		} else {
			try {
				bool res = run_in_worker(wrk, [&] {
					return trader.lock()->eraseTrade(args[1],trunc);
				});
				if (!res) {
					stream << "Trade not found: " << args[1] << std::endl;
					return 2;
				} else {
					stream << "OK" << std::endl;
					return 0;
				}
			} catch (std::exception &e) {
				stream << e.what() << std::endl;;
				return 3;
			}
		}
	}
}

static int cmd_singlecmd(Worker &wrk, simpleServer::ArgList args, std::ostream &stream, void (MTrader::*fn)()) {
	if (args.empty()) {
		stream << "Need argument: <trader_ident>\n"; return 1;
	}
	auto trader = traders.lock_shared()->find(args[0]).lock();
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




static ondra_shared::CrashHandler report_crash([](const char *line) {
	ondra_shared::logFatal("CrashReport: $1", line);
});


class App: public ondra_shared::DefaultApp {
public:

	App(): ondra_shared::DefaultApp({
		App::Switch{'t',"dry_run",[this](auto &&){this->test = true;},"dry run"},
		App::Switch{'p',"port",[this](auto &&cmd){
			auto p = cmd.getUInt();
			if (p.has_value())
				this->port = *p;
			else
				throw std::runtime_error("Need port number after -p");
			},"<number> Temporarily opens TCP port"},
	},std::cout) {}

	bool test = false;
	int port = -1;

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
				"get_all_pairs- print all tradable pairs - need broker name as argument",
				"erase_trade  - erases trade. Need id of trader and id of trade",
				"reset        - erases all trades expect the last one",
				"repair       - repair pair",
				"admin        - generate temporary admin login and password",
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
					[&](simpleServer::ServiceControl cntr, ondra_shared::StrViewA name, simpleServer::ArgList arglist) mutable {

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


						auto storagePath = servicesection.mandatory["storage_path"].getPath();
						auto storageBinary = servicesection["storage_binary"].getBool(true);
						auto storageBroker = servicesection["storage_broker"];
						auto storageVersions = servicesection["storage_versions"].getUInt(5);
						auto listen = servicesection["listen"].getString();
						auto socket = servicesection["socket"].getPath();
						auto brk_timeout = servicesection["broker_timeout"].getInt(10000);
						auto rptsect = app.config["report"];
						auto rptpath = rptsect.mandatory["path"].getPath();
						auto rptinterval = rptsect["interval"].getUInt(864000000);
						auto dr = rptsect["report_broker"];
						auto isim = rptsect["include_simulators"].getBool(false);
						auto asyncProvider = simpleServer::ThreadPoolAsync::create(2,1);

						Strategy::setConfig(app.config["strategy"]);



						PStorageFactory sf;

						if (!storageBroker.defined()) {
							sf = PStorageFactory(new StorageFactory(storagePath,storageVersions,storageBinary?Storage::binjson:Storage::json));
						} else {
							sf = PStorageFactory(new ExtStorage(storageBroker.getCurPath(), "storage_broker", storageBroker.getString(), brk_timeout));
							auto bl = servicesection["backup_locally"].getBool(false);
							if (bl) {
								PStorageFactory sf2 (new StorageFactory(storagePath,storageVersions,storageBinary?Storage::binjson:Storage::json));
								sf = PStorageFactory (new BackedStorageFactory(std::move(sf), std::move(sf2)));
							}
						}

						StorageFactory rptf(rptpath,2,Storage::json);

						PReport rpt = PReport::make(rptf.create("report.json"), rptinterval);


						PPerfModule perfmod;
						if (dr.defined())
						{
							std::string cmdline;
							std::string workdir;
							cmdline = dr.getPath();
							workdir = dr.getCurPath();
							perfmod = SharedObject<ExtDailyPerfMod>::make(workdir,"performance_module", cmdline, isim, brk_timeout).cast<IDailyPerfModule>();
						} else {
							perfmod = SharedObject<LocalDailyPerfMonitor>::make(sf->create("_performance_daily"), storagePath+"/_performance_current",isim).cast<IDailyPerfModule>();
						}



						Worker wrk = schedulerGetWorker(sch);


						traders = traders.make(
								sch,app.config["brokers"], app.test,sf,rpt,perfmod, rptpath,  brk_timeout
						);

						RefCntPtr<AuthUserList> aul;

						SharedObject<WebCfg::State> webcfgstate;
						StrViewA webadmin_auth = servicesection["admin"].getString();
						webcfgstate = SharedObject<WebCfg::State>::make(sf->create("web_admin_conf"),new AuthUserList, new AuthUserList);
						webcfgstate.lock()->setAdminAuth(webadmin_auth);
						webcfgstate.lock()->applyConfig(traders);
						aul = webcfgstate.lock_shared()->users;

						std::unique_ptr<simpleServer::MiniHttpServer> srv;

						simpleServer::NetAddr addr(nullptr);
						if (!socket.empty()) {
							addr = simpleServer::NetAddr::create(std::string("unix:")+socket,11223);
						}
						if (!listen.empty()) {
							simpleServer::NetAddr baddr = simpleServer::NetAddr::create(listen,11223);
							if (addr.getHandle() == nullptr) addr = baddr;
							else addr = addr + baddr;
						}

						if (app.port>0) {
							simpleServer::NetAddr baddr =  simpleServer::NetAddr::create("0",app.port);
							if (addr.getHandle() == nullptr) addr = baddr;
							else addr = addr + baddr;
						}

						if (addr.getHandle() != nullptr) {
							srv = std::make_unique<simpleServer::MiniHttpServer>(addr, asyncProvider);


							std::vector<simpleServer::HttpStaticPathMapper::MapRecord> paths;
							paths.push_back(simpleServer::HttpStaticPathMapper::MapRecord{
								"/",AuthMapper(name,aul) >>= simpleServer::HttpFileMapper(std::string(rptpath), "index.html")
							});

							paths.push_back({
								"/admin",ondra_shared::shared_function<bool(simpleServer::HTTPRequest, ondra_shared::StrViewA)>(WebCfg(webcfgstate,
										name,
										traders,
										[=](WebCfg::Action &&a) mutable {sch.immediate() >> std::move(a);}))
							});
							(*srv)  >>=  simpleServer::HttpStaticPathMapperHandler(paths);
						}



						logNote("---- Starting service ----");

						cntr.on("get_all_pairs") >> [&](auto &&args, std::ostream &stream){
							if (args.length < 1) {
								stream << "Append argument: <broker>" << std::endl;
								return 1;
							} else {
								StockSelector ss;
								ss.loadBrokers(app.config["brokers"], true, brk_timeout);
								IStockApi *stock = ss.getStock(args[0]);
								if (stock) {
									for (auto &&k : stock->getAllPairs()) {
										stream << k << std::endl;
									}
									return 0;
								} else {
									stream << "Stock is not defined" << std::endl;
									return 2;
								}
							}

						};

						cntr.on("erase_trade") >> [&](auto &&args, std::ostream &out){
							return eraseTradeHandler(wrk, args,out,false);
						};
						cntr.on("reset") >> [&](auto &&args, std::ostream &out){
							return cmd_singlecmd(wrk, args,out,&MTrader::reset);
						};
						cntr.on("repair") >> [&](auto &&args, std::ostream &out){
							return cmd_singlecmd(wrk, args,out,&MTrader::repair);
						};
						cntr.on("admin") >> [&](auto &&, std::ostream &out){
							std::random_device rnd;
							std::uniform_int_distribution<int> dist(33,126);
							std::ostringstream buff;
							for (unsigned int i = 0; i < 16; i++) buff<<static_cast<char>(dist(rnd));
							std::string lgn = buff.str();
							webcfgstate.lock()->setAdminUser("admin",lgn);
							out << "Username: admin" << std::endl << "Password: " << lgn << std::endl;
							return 0;
						};
						cntr.on_run() >> [=]() mutable {

							ondra_shared::PStdLogProviderFactory current =
									&dynamic_cast<ondra_shared::StdLogProviderFactory &>(*ondra_shared::AbstractLogProviderFactory::getInstance());
							ondra_shared::PStdLogProviderFactory logcap = Report::captureLog(rpt, current);

							sch.immediate() >> [logcap]{
								ondra_shared::AbstractLogProvider::getInstance() = logcap->create();
							};



							auto report_cycle = [=]() mutable {
								auto rptl = rpt.lock();
								rptl->perfReport(perfmod.lock()->getReport());
								rptl->genReport();
							};

							auto trader_cycle = [=]() mutable {
								traders.lock()->resetBrokers();
								traders.lock_shared()->enumTraders([&](const auto & trinfo){
									sch.immediate()>>[tr = trinfo.second]()mutable{
										try {
											tr.lock()->perform(false);
										} catch (std::exception &e) {
											logError("Scheduler exception: $1", e.what());
										}
									};
								});
								sch.after(std::chrono::seconds(1)) >> report_cycle;

							};


							sch.after(std::chrono::seconds(1)) >> trader_cycle;
							sch.each(std::chrono::minutes(1)) >> trader_cycle;


							return 0;
						};


						cntr.dispatch();

						sch.removeAll();
						logNote("---- Waiting to finish cycle ----");
						sch.sync();
						traders.lock()->clear();
					}
					logNote("---- Exit ----");


					return 0;

					}, simpleServer::ArgList(argList.data(), argList.size()),cmd != "admin");
			} catch (std::exception &e) {
				std::cerr << "Error: " << e.what() << std::endl;
				return 2;
			}
		} else {
			std::cout << "use -h for help" << std::endl;
		}
	} catch (std::exception &e) {
		std::cerr << "Error:" << e.what() << std::endl;
		return 1;
	}
}
