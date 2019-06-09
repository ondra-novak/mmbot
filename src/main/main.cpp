#include <shared/scheduler.h>
#include <simpleServer/abstractService.h>
#include <shared/stdLogFile.h>
#include <algorithm>
#include <iostream>

#include "../server/src/simpleServer/abstractStream.h"
#include "../server/src/simpleServer/address.h"
#include "../server/src/simpleServer/http_filemapper.h"
#include "../server/src/simpleServer/http_server.h"

#include "shared/ini_config.h"
#include "shared/shared_function.h"
#include "shared/cmdline.h"
#include "shared/future.h"
#include "../shared/sch2wrk.h"
#include "istockapi.h"
#include "istatsvc.h"
#include "ordergen.h"
#include "mtrader.h"
#include "report.h"
#include "spread_calc.h"
#include "ext_stockapi.h"
#include "notrade.h"


using ondra_shared::StdLogFile;
using ondra_shared::StrViewA;
using ondra_shared::LogLevel;
using ondra_shared::logNote;
using ondra_shared::logInfo;
using ondra_shared::logProgress;
using ondra_shared::logError;
using ondra_shared::logDebug;
using ondra_shared::LogObject;
using ondra_shared::shared_function;
using ondra_shared::parseCmdLine;
using ondra_shared::Scheduler;
using ondra_shared::Worker;
using ondra_shared::schedulerGetWorker;


class StatsSvc: public IStatSvc {
public:
	StatsSvc(std::string name, Report &rpt, int interval, int cnt ):rpt(rpt),name(name),interval(interval),cnt(cnt) {}

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.setOrders(name, buy, sell);
	}
	virtual void reportTrades(double cur_balance, ondra_shared::StringView<IStockApi::Trade> trades) {
		rpt.setTrades(name,cur_balance, trades);
	}

	virtual void setInfo(StrViewA title,StrViewA asst,StrViewA curc, bool emulated) {
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

		if (cnt) {
			--cnt;
			return prev_value;
		} else {
			cnt = interval;
			return  glob_calcSpread(chart, cfg, minfo, balance, prev_value);
		}

	}

	Report &rpt;
	std::string name;
	int interval;
	mutable int cnt = 0;


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
			data.push_back(StockMarketMap::value_type(name,std::make_unique<ExtStockApi>(workDir, name, cmdline, test)));
		}
		data.push_back(StockMarketMap::value_type("notrade",std::make_unique<NoTrade>()));
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
};



static std::vector<NamedMTrader> traders;
static StockSelector stockSelector;


void loadTraders(const ondra_shared::IniConfig &ini,
		ondra_shared::StrViewA names, StorageFactory &sf, Report &rpt, bool force_dry_run) {
	traders.clear();
	std::vector<StrViewA> nv;

	auto nspl = names.split(" ");
	while (!!nspl) {
		StrViewA x = nspl();
		if (!x.empty()) nv.push_back(x);
	}

	int p = 0;
	for (auto n: nv) {
		if (n[0] == '_') throw std::runtime_error(std::string(n).append(": The trader's name can't begins with underscore '_'"));
		MTrader::Config mcfg = MTrader::load(ini[n], force_dry_run);
		logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
		traders.emplace_back(stockSelector, sf.create(n),
				std::make_unique<StatsSvc>(n, rpt, nv.size(), ++p),
				mcfg, n);
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


class AuthMapper {
public:

	AuthMapper(	std::string users, std::string realm):users(users),realm(realm) {}
	AuthMapper &operator >>= (simpleServer::HTTPHandler &&hndl) {
		handler = std::move(hndl);
		return *this;
	}

	void operator()(simpleServer::HTTPRequest req) const {
		if (!users.empty()) {
			auto hdr = req["Authorization"];
			auto hdr_splt = hdr.split(" ");
			StrViewA type = hdr_splt();
			StrViewA cred = hdr_splt();
			if (type != "Basic") return genError(req);
			auto u_splt = StrViewA(users).split(" ");
			bool found = false;
			while (!!u_splt && !found) {
				StrViewA u = u_splt();
				found = u == cred;
			}
			if (!found) return genError(req);
		}
		handler(req);
	}

	void genError(simpleServer::HTTPRequest req) const {
		req.sendResponse(simpleServer::HTTPResponse(401)
			.contentType("text/html")
			("WWW-Authenticate","Basic realm=\""+realm+"\""),
			"<html><body><h1>401 Unauthorized</h1></body></html>"
			);
	}

protected:
	AuthMapper(	std::string users, std::string realm, simpleServer::HTTPHandler &&handler):users(users), handler(std::move(handler)) {}
	std::string users;
	std::string realm;
	simpleServer::HTTPHandler handler;
};

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
			ondra_shared::Countdown wt(1);
			bool res;
			wrk >> [&] {
				res = trader.eraseTrade(args[1],trunc);
				wt.dec();
			};
			wt.wait();
			if (!res) {
				stream << "Trade not found: " << args[1] << "\n";
				return 2;
			} else {
				stream << "OK\n";
				return 0;
			}
		}
	}
}

int main(int argc, char **argv) {

	auto refdir = std::experimental::filesystem::current_path();
	bool verbose = false;
	bool debug = false;
	bool test = false;

	auto cmdln = parseCmdLine(argc, argv);
	auto app_path = cmdln.getProgramFullPath();
	auto cfgname = (app_path.parent_path().parent_path() / "conf" / (app_path.filename().string() +".conf"));


	while (!!cmdln) {
		char z = cmdln.getOpt();
		switch (z) {
		case 'f': cfgname = cmdln.getNext();break;
		case 'h': std::cout << "Usage: " << std::endl << "\t" << argv[0]
					<< " [-vdt] -f <cfgname>  <cmd>" << std::endl << "\t" << argv[0]
					<< " -h" << std::endl << std::endl
					<< "-f\tspecift config pathname" << std::endl
					<< "-h\tthis help" << std::endl
					<< "-v\tverbose - redirect log to stderr (only for 'run' command)" << std::endl
					<< "-d\tdebug - force debug level of logging" << std::endl
					<< "-t\tdry run (test) - do not execute orders" << std::endl
					<< "<cmd>\tcommand to control service" << std::endl
					<< "\t\tstart        - start service on background"<< std::endl
					<< "\t\tstop         - stop service "<< std::endl
					<< "\t\trestart      - restart service "<< std::endl
					<< "\t\trun          - start service at foreground"<< std::endl
					<< "\t\tstatus       - print status"<< std::endl
					<< "\t\tpidof        - print pid"<< std::endl
					<< "\t\twait         - wait until service exits"<< std::endl
					<< "\t\tlogrotate    - close and reopen logfile"<< std::endl
					<< "\t\tcalc_range   - calculate and print trading range for each pair"<< std::endl
					<< "\t\tget_all_pairs- print all tradable pairs - need broker name as argument"<< std::endl
					<< std::endl;
					return 1;
		case 'v': verbose = true;break;
		case 't': test = true;break;
		case 'd': debug = true;break;
		default: std::cerr << "Unknown option -" << z << std::endl;
					return 1;
		case 0: try {
			StrViewA cmd = cmdln.getNext();

			ondra_shared::IniConfig ini;
			auto cfgpath = std::experimental::filesystem::absolute(cfgname, refdir);
			ini.load(cfgpath);

			auto servicesection = ini["service"];
			auto pidfile = servicesection.mandatory["inst_file"].getPath();
			auto name = servicesection["name"].getString("markettrader");
			auto user = servicesection["user"].getString();

			std::vector<StrViewA> argList;
			while (!!cmdln) argList.push_back(cmdln.getNext());

			return simpleServer::ServiceControl::create(name, pidfile, cmd,
				[&](simpleServer::ServiceControl cntr, ondra_shared::StrViewA name, simpleServer::ArgList arglist) {

					if (verbose && cntr.isDaemon()) {
						std::cerr << "Verbose is not avaiable in daemon mode" << std::endl;
						return 100;
					}

					cntr.enableRestart();
					if (!user.empty()) {
						cntr.changeUser(user);
					}

					{
						auto logcfg = ini["log"];
						auto log = StdLogFile::create(
										verbose?std::string(""):logcfg["file"].getPath(""),
										debug?StrViewA(""):logcfg["level"].getString(""),
										LogLevel::debug);
						log->setDefault();
						cntr.addCommand("logrotate",[=](const simpleServer::ArgList &, simpleServer::Stream ) {
							ondra_shared::logRotate();
							return 0;
						});

					}


					auto lstsect = ini["traders"];
					auto names = lstsect.mandatory["list"].getString();
					auto storagePath = lstsect.mandatory["storage_path"].getPath();
					auto rptsect = ini["report"];
					auto rptpath = rptsect.mandatory["path"].getPath();
					auto rptinterval = rptsect["interval"].getUInt(864000000);

					stockSelector.loadStockMarkets(ini["brokers"], test);

					auto web_bind = rptsect["http_bind"];

					std::unique_ptr<simpleServer::MiniHttpServer> srv;

					if (web_bind.defined()) {
						simpleServer::NetAddr addr = simpleServer::NetAddr::create(web_bind.getString(),11223);
						srv = std::make_unique<simpleServer::MiniHttpServer>(addr, 1, 1);
						(*srv)  >>= AuthMapper(rptsect["http_auth"].getString(),name)
								>>= simpleServer::HttpFileMapper(std::string(rptpath), "index.html");
					}


					StorageFactory sf(storagePath);
					StorageFactory rptf(rptpath,2,Storage::json);

					Report rpt(rptf.create("report.json"), rptinterval);



					Scheduler sch = ondra_shared::Scheduler::create();
					Worker wrk = schedulerGetWorker(sch);


					loadTraders(ini, names, sf, rpt, test);

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
											<< "\tMin price:\t\t" << result.min_price << " " << curs << std::endl
											<< "\t- Money left:\t\t" << result.money_left << " " << curs << std::endl
											<< "\tMax price:\t\t" << result.max_price << " " << curs << std::endl
											<< "\t- Assets left:\t\t" << result.assets_left<< " " << ass << std::endl;
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
							ss.loadStockMarkets(ini["brokers"], true);
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
					std::size_t id = 0;
					cntr.addCommand("run",[&](simpleServer::ArgList, simpleServer::Stream) {

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

					logNote("---- Exit ----");

					return 0;

				}, simpleServer::ArgList(argList.data(), argList.size()),
				cmd == "calc_range" || cmd == "get_all_pairs");
		} catch (std::exception &e) {
			std::cerr << "Error: " << e.what() << std::endl;
			return 2;
		};break;
		}

	}
	std::cerr << "Missing arguments. Use -h to show help" << std::endl;
	return 1;
}
