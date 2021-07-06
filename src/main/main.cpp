#include <shared/scheduler.h>
#include <simpleServer/abstractService.h>
#include <shared/stdLogFile.h>
#include <shared/default_app.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <imtjson/parser.h>

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
#include "../server/src/simpleServer/http_hostmapping.h"
#include "../server/src/simpleServer/threadPoolAsync.h"
#include "ext_storage.h"
#include "extdailyperfmod.h"
#include "localdailyperfmod.h"
#include "stats2report.h"
#include "traders.h"
#include "../../version.h"

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


static ondra_shared::CrashHandler report_crash([](const char *line) {
	ondra_shared::logFatal("CrashReport: $1", line);
});


class App: public ondra_shared::DefaultApp {
public:

	App(): ondra_shared::DefaultApp({
		App::Switch{'t',"dry_run",[this](auto &&){this->test = true;},"dry run"},
		App::Switch{'V',"version",[this](auto &&cmd){
			print_ver = true;
		},"print version"},
		App::Switch{'p',"port",[this](auto &&cmd){
			auto p = cmd.getUInt();
			if (p.has_value())
				this->port = *p;
			else
				throw std::runtime_error("Need port number after -p");
			},"<number> Temporarily opens TCP port"},
	},std::cout) {}

	bool test = false;
	bool print_ver = false;
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

void trader_cycle(PReport rpt, PPerfModule perfmod, Scheduler sch, int pos, std::chrono::steady_clock::time_point nextRun) {

	if (pos == 0) {
		nextRun = nextRun+std::chrono::minutes(1);
		traders.lock()->resetBrokers();
	}

	SharedObject<NamedMTrader> selected;
	int p = pos;
	traders.lock_shared()->enumTraders([&](const auto & trinfo){
		if (p == 0) selected = trinfo.second;
		p--;
	});
	if (selected == nullptr) {
		auto rptl = rpt.lock();
		rptl->perfReport(perfmod.lock()->getReport());
		rptl->genReport();
		sch.at(nextRun) >> [=]{
			trader_cycle(rpt,perfmod,  sch, 0, nextRun);
		};
	} else {
		try {
			auto t1 = std::chrono::system_clock::now();
			auto tl = selected.lock();
			std::string_view ident = tl->ident;
			tl->perform(false);
			tl.release();
			auto t2 = std::chrono::system_clock::now();
			traders.lock()->report_util(ident, std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count());
		} catch (std::exception &e) {
			logError("Scheduler exception: $1", e.what());
		}
		sch.after(std::chrono::milliseconds(1)) >> [=]{
			trader_cycle(rpt,perfmod,  sch, pos+1, nextRun);
		};
	}
}


int main(int argc, char **argv) {

	json::enableParsePreciseNumbers = true;
	try {

		App app;

		if (!app.init(argc, argv)) {
			std::cerr << "Invalid parameters at:" << app.args->getNext() << std::endl;
			return 1;
		}

		if (app.print_ver) {
			std::cout << MMBOT_VERSION << std::endl;
			return 0;
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
						auto upload_limit = servicesection["upload_limit"].getUInt(10*1024*1024);
						auto brk_timeout = servicesection["broker_timeout"].getInt(10000);
						auto rptsect = app.config["report"];
						auto rptpath = rptsect.mandatory["path"].getPath();
						auto rptinterval = rptsect["interval"].getUInt(864000000);
						auto dr = rptsect["report_broker"];
						auto isim = rptsect["include_simulators"].getBool(false);
						auto asyncProvider = simpleServer::ThreadPoolAsync::create(2,1);
						auto login_section = app.config["login"];
						auto backtest_section = app.config["backtest"];
						auto history_broker = backtest_section.mandatory["history_source"];
						auto news_url=app.config["news"]["url"].getString();




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

						PReport rpt = PReport::make(rptf.create("report.json"), ReportConfig{rptinterval,news_url});


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

						StrViewA webadmin_auth = login_section["admin"].getString();
						json::PJWTCrypto jwt;
						{
							auto jwt_type = login_section["jwt_type"].getString();
							auto jwt_pubkey = login_section["jwt_pubkey"].getPath();
							if (!jwt_type.empty() && !jwt_pubkey.empty()) {
								jwt=AuthMapper::initJWT(jwt_type, jwt_pubkey);
							}
						}
						SharedObject<WebCfg::State> webcfgstate = SharedObject<WebCfg::State>::make(sf->create("web_admin_conf"),new AuthUserList, new AuthUserList);
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
							auto phb = SharedObject<AbstractExtern>::make(history_broker.getCurPath(), "history_broker", history_broker.getString(), 55000);

							srv = std::make_unique<simpleServer::MiniHttpServer>(addr, asyncProvider);


							std::vector<simpleServer::HttpStaticPathMapper::MapRecord> paths;
							paths.push_back(simpleServer::HttpStaticPathMapper::MapRecord{
								"/",AuthMapper(name,aul,jwt, true) >>= simpleServer::HTTPMappedHandler(simpleServer::HttpFileMapper(std::string(rptpath), "index.html", 600))
							});

							paths.push_back({
								"/admin",ondra_shared::shared_function<bool(simpleServer::HTTPRequest, ondra_shared::StrViewA)>(WebCfg(webcfgstate,
										name,
										traders,
										[=](WebCfg::Action &&a) mutable {sch.immediate() >> std::move(a);},jwt, phb, upload_limit))
							});
							paths.push_back({
								"/set_cookie",[](simpleServer::HTTPRequest req, const ondra_shared::StrViewA &) mutable {
									return AuthMapper::setCookieHandler(req);
								}
							});
							(*srv)  >>=  (simpleServer::AutoHostMappingHandler() >> simpleServer::HttpStaticPathMapperHandler(paths));

						}



						logNote("---- Starting service ----");

						cntr.on("get_all_pairs") >> [&](auto &&args, std::ostream &stream){
							if (args.length < 1) {
								stream << "Append argument: <broker>" << std::endl;
								return 1;
							} else {
								StockSelector ss;
								ss.loadBrokers(app.config["brokers"], true, brk_timeout);
								PStockApi stock = ss.getStock(args[0]);
								if (stock != nullptr) {
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


							trader_cycle(rpt, perfmod, sch, -1, std::chrono::steady_clock::now());

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
