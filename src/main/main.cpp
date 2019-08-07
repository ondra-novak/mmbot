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
using ondra_shared::schedulerGetWorker;


class StatsSvc: public IStatSvc {
public:
	StatsSvc(Worker wrk, std::string name, Report &rpt, int interval, int cnt ):wrk(wrk),rpt(rpt),name(name),interval(interval),cnt(cnt)
		,spread(std::make_shared<double>(0)) {}

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.setOrders(name, buy, sell);
	}
	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades, bool margin) {
		rpt.setTrades(name,trades,margin);
	}
	virtual void reportMisc(const MiscData &miscData) {
		rpt.setMisc(name, miscData);
	}
	virtual void reportError(const char *what) {
		rpt.setError(name, what);
	}

	virtual void setInfo(StrViewA title,StrViewA asst,StrViewA curc, bool emulated) {
		if (title.empty()) title = name;
		rpt.setInfo(name, title, asst, curc, emulated);
	}
	virtual void reportPrice(double price) {
		rpt.setPrice(name, price);
	}
	virtual double calcSpread(ondra_shared::StringView<ChartItem> chart,
			const MTrader_Config &cfg,
			const IStockApi::MarketInfo &minfo,
			double balance,
			double prev_value) const {

		if (*spread == 0) *spread = prev_value;

		if (cnt && *spread != 0) {
			--cnt;
			return *spread;
		} else {
			cnt += interval;
			wrk >> [chart = std::vector<ChartItem>(chart.begin(),chart.end()),
					cfg = MTrader_Config(cfg),
					minfo = IStockApi::MarketInfo(minfo),
					balance,
					spread = this->spread,
					name = this->name] {
				LogObject logObj(name);
				LogObject::Swap swap(logObj);
				*spread = glob_calcSpread(chart, cfg, minfo, balance, *spread);
			};
			return *spread;
		}

	}

	Worker wrk;
	Report &rpt;
	std::string name;
	int interval;
	mutable int cnt = 0;
	std::shared_ptr<double> spread;


};


class NamedMTrader: public MTrader {
public:
	NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, Config cfg, std::string &&name)
			:MTrader(sel, std::move(storage), std::move(statsvc), cfg), ident(std::move(name)) {
	}

	bool perform() {
		LogObject lg(ident);
		LogObject::Swap swap(lg);
		try {
			return MTrader::perform();
		} catch (std::exception &e) {
			logError("$1", e.what());
			return false;
		}
	}

	std::string ident;

};

class StockSelector: public IStockSelector{
public:
	using PStockApi = std::unique_ptr<IStockApi>;
	using StockMarketMap =  ondra_shared::linear_map<std::string, PStockApi, std::less<>>;

	StockMarketMap stock_markets;

	void loadStockMarkets(const ondra_shared::IniConfig::Section &ini, bool test) {
		std::vector<StockMarketMap::value_type> data;
		for (auto &&def: ini) {
			ondra_shared::StrViewA name = def.first;
			ondra_shared::StrViewA cmdline = def.second.getString();
			ondra_shared::StrViewA workDir = def.second.getCurPath();
			data.push_back(StockMarketMap::value_type(name,std::make_unique<ExtStockApi>(workDir, name, cmdline)));
		}
		StockMarketMap map(std::move(data));
		stock_markets.swap(map);
	}
	virtual IStockApi *getStock(const std::string_view &stockName) const {
		auto f = stock_markets.find(stockName);
		if (f == stock_markets.cend()) return nullptr;
		return f->second.get();
	}
	void addStockMarket(ondra_shared::StrViewA name, PStockApi &&market) {
		stock_markets.insert(std::pair(name,std::move(market)));
	}

	virtual void forEachStock(EnumFn fn)  const {
		for(auto &&x: stock_markets) {
			fn(x.first, *x.second);
		}
	}
	void clear() {
		stock_markets.clear();
	}
};



static std::vector<NamedMTrader> traders;
static StockSelector stockSelector;


void loadTraders(const ondra_shared::IniConfig &ini,
		ondra_shared::StrViewA names, StorageFactory &sf,
		Worker wrk, Report &rpt, bool force_dry_run) {
	traders.clear();
	std::vector<StrViewA> nv;

	auto nspl = names.split(" ");
	while (!!nspl) {
		StrViewA x = nspl();
		if (!x.empty()) nv.push_back(x);
	}

	int p = 0;
	for (auto n: nv) {
		LogObject lg(n);
		LogObject::Swap swp(lg);
		try {
			if (n[0] == '_') throw std::runtime_error(std::string(n).append(": The trader's name can't begins with underscore '_'"));
			MTrader::Config mcfg = MTrader::load(ini[n], force_dry_run);
			logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
			traders.emplace_back(stockSelector, sf.create(n),
					std::make_unique<StatsSvc>(wrk, n, rpt, nv.size(), ++p),
					mcfg, n);
		} catch (const std::exception &e) {
			logFatal("Error: $1", e.what());
			throw std::runtime_error(std::string("Unable to initialize trader: ").append(n).append(" - ").append(e.what()));
		}
	}
}

bool runTraders() {
	stockSelector.forEachStock([](json::StrViewA, IStockApi&api) {
		api.reset();
	});

	bool hit = false;
	for (auto &&t : traders) {
		bool h = t.perform();
		hit |= h;
	}
	return hit;
}


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
		auto iter = std::find_if(traders.begin(), traders.end(),[&](const NamedMTrader &tr) {
			return StrViewA(tr.ident) == args[0];
		});
		if (iter == traders.end()) {
			stream << "Trader idenitification is invalid: " << args[0] << "\n";
			return 2;
		} else {
			NamedMTrader  &trader = *iter;
			try {
				bool res = run_in_worker(wrk, [&] {
					return trader.eraseTrade(args[1],trunc);
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
	StrViewA trader = args[0];
	auto iter = std::find_if(traders.begin(), traders.end(), [&](const NamedMTrader &dr){
		return StrViewA(dr.ident) == trader;
	});
	if (iter == traders.end()) {
		stream << "Trader idenitification is invalid: " << trader << "\n";
		return 1;
	}
	try {
		MTrader &t = *iter;
		run_in_worker(wrk, [&]{
			(t.*fn)();return true;
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

	StrViewA trader = args[0];
	auto iter = std::find_if(traders.begin(), traders.end(), [&](const NamedMTrader &dr){
		return StrViewA(dr.ident) == trader;
	});
	if (iter == traders.end()) {
		stream << "Trader idenitification is invalid: " << trader << "\n";
		return 1;
	}
	try {
		NamedMTrader &t = *iter;
		run_in_worker(wrk, [&]{
			t.achieve_balance(price,balance);return true;
		});
		stream << "OK\n";
		return 0;
	} catch (std::exception &e) {
		stream << e.what() << "\n";
		return 3;
	}
}

class App: public ondra_shared::DefaultApp {
public:

	using ondra_shared::DefaultApp::DefaultApp;


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
				"repair       - repair pair"
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
		bool test = false;
//		auto refdir = std::experimental::filesystem::current_path();


		App app({
			App::Switch{'t',"dry_run",[&](auto &&){test = true;},"dry run"},
		},std::cout);

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

				return simpleServer::ServiceControl::create(name, pidfile, cmd,
					[&](simpleServer::ServiceControl cntr, ondra_shared::StrViewA name, simpleServer::ArgList arglist) {

						if (app.verbose && cntr.isDaemon()) {

							std::cerr << "Verbose is not avaiable in daemon mode" << std::endl;
							return 100;
						}

						if (!user.empty()) {
							cntr.changeUser(user);
						}

						cntr.enableRestart();

						cntr.addCommand("logrotate",[=](const simpleServer::ArgList &, simpleServer::Stream ) {
							ondra_shared::logRotate();
							return 0;
						});



						auto lstsect = app.config["traders"];
						auto names = lstsect.mandatory["list"].getString();
						auto storagePath = lstsect.mandatory["storage_path"].getPath();
						auto rptsect = app.config["report"];
						auto rptpath = rptsect.mandatory["path"].getPath();
						auto rptinterval = rptsect["interval"].getUInt(864000000);
						auto a2np = rptsect["a2np"].getBool(false);

						stockSelector.loadStockMarkets(app.config["brokers"], test);

						auto web_bind = rptsect["http_bind"];

						std::unique_ptr<simpleServer::MiniHttpServer> srv;


					if (web_bind.defined()) {
						simpleServer::NetAddr addr = simpleServer::NetAddr::create(web_bind.getString(),11223);
						srv = std::make_unique<simpleServer::MiniHttpServer>(addr, 1, 1);

						std::vector<simpleServer::HttpStaticPathMapper::MapRecord> paths;
						paths.push_back(simpleServer::HttpStaticPathMapper::MapRecord{
							"/",AuthMapper(rptsect["http_auth"].getString(),name)
										>>= simpleServer::HttpFileMapper(std::string(rptpath), "index.html")
						});

						auto webcfgsect = app.config["webcfg"];
						auto webcfg_enabled = webcfgsect["enabled"];
						if (webcfg_enabled.getBool(false)) {
							std::string path = webcfg_enabled.getCurPath();
							path.append(app.config.pathSeparator.data, app.config.pathSeparator.length)
								.append("webcfg.conf");
							std::string auth = webcfgsect["auth"].getString();
							auto restartFn = [=]{
								std::string switches;
								switches.push_back('-');
								if (test) switches.push_back('t');
								if (app.debug) switches.push_back('d');
								switches.push_back('f');
								std::string appPath = app.appPath.string();
								std::string configPath = app.configPath.string();
								const char *args[] = {
										appPath.c_str(),
										switches.c_str(),
										configPath.c_str(),
										"restart",
										nullptr
								};
								spawn(appPath.c_str(), args);
							};
							paths.push_back({
								"/admin",WebCfg(auth,name,path,webcfgsect["serial"].getUInt(0),stockSelector,restartFn)
							});
						}

						(*srv)  >>=  simpleServer::HttpStaticPathMapperHandler(paths);
					}




						StorageFactory sf(storagePath);
						StorageFactory rptf(rptpath,2,Storage::json);

						Report rpt(rptf.create("report.json"), rptinterval, a2np);



						Scheduler sch = ondra_shared::Scheduler::create();
						Worker wrk = schedulerGetWorker(sch);


						loadTraders(app.config, names, sf,wrk, rpt, test);

						logNote("---- Starting service ----");

						cntr.addCommand("calc_range",[&](const simpleServer::ArgList &args, simpleServer::Stream out){

							ondra_shared::Countdown cnt(1);
							wrk >> [&] {
								try {
									for(auto &&t:traders) {							;
										std::ostringstream buff;
										auto result = t.calc_min_max_range();
										auto ass = t.getMarketInfo().asset_symbol;
										auto curs = t.getMarketInfo().currency_symbol;
										buff << "Trader " << t.getConfig().title
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
								cnt.dec();
							};
							cnt.wait();

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
									runTraders();
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

					traders.clear();
					stockSelector.clear();

					logNote("---- Exit ----");

					return 0;

					}, simpleServer::ArgList(argList.data(), argList.size()),
					cmd == "calc_range" || cmd == "get_all_pairs" || cmd == "achieve" || cmd == "reset" || cmd=="repair");
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
