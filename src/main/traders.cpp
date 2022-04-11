/*
 * traders.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#include "traders.h"

#include "trader_factory.h"

#include "errhandler.h"

Traders::Traders(PBrokerList brokers,
				PStorageFactory sf,
				PHistStorageFactory<HistMinuteDataItem> hsf,
				PReport rpt,
				PPerfModule perfMod,
				PBalanceMap balanceCache,
				PBalanceMap extBalances)
:brokers(brokers)
,sf(sf)
,rpt(rpt)
,perfMod(perfMod)
,balanceCache(balanceCache)
,extBalances(extBalances)
,hsf(hsf)
{
}


struct State {

	std::chrono::system_clock::time_point start;
	std::atomic<std::size_t> counter;
	userver::Callback<void(bool)> done;
	PUtilization utls;
	State(std::chrono::system_clock::time_point start, std::size_t sz, userver::Callback<void(bool)> &&done, PUtilization utls)
		:start(start),counter(sz),done(std::move(done)),utls(utls) {}

};


void Traders::run_cycle(ondra_shared::Worker wrk, PUtilization utls, userver::Callback<void(bool)> &&done) {
	auto &stop = this->stop;

	if (traders.empty()) {
		done(!stop.load());
		return;
	}

	if (stop.load()) {done(false);return;}
	std::unique_lock lk(mx);
	if (stop.load()) {done(false);return;}
	last_reset = std::chrono::system_clock::now();
	auto st = std::make_shared<State>(
		last_reset,
		traders.size(),
		std::move(done),
		utls
	);


	for (auto &t: traders) {
		wrk >> [&stop,  trader = t.second, name = std::string_view(t.first), st]() mutable {
			try {
				auto trstr = std::chrono::system_clock::now();
				auto trlk = trader.lock();
				if (!stop.load()) {
					trlk->reset_broker(st->start);
					if (!stop.load()) {
						trlk->run();
						auto trend = std::chrono::system_clock::now();
						st->utls.lock()->report_trader(name, std::chrono::duration_cast<std::chrono::milliseconds>(trstr - trend));
					}
				}
			} catch (...) {
				REPORT_UNHANDLED();
			}
			if (--st->counter == 0) {
				auto end = std::chrono::system_clock::now();
				st->utls.lock()->report_overall(std::chrono::duration_cast<std::chrono::milliseconds>(end - st->start));
				st->done(!stop.load());
			}
		};
	}
}

void Traders::stop_cycle() {
	stop.store(true);
}

void Traders::begin_add() {
	prepared.clear();
	prepared_trader_conflict_map = prepared_trader_conflict_map.make();
	prepared_walletDB = prepared_walletDB.make();
}

void Traders::add_trader(std::string_view id, const json::Value &config) {
	Trader_Config_Ex tcfg;
	tcfg.parse(config);
	Trader_Env env;
	env.balanceCache = balanceCache;
	env.conflicts = prepared_trader_conflict_map;
	env.exchange = brokers->get_broker(tcfg.broker);
	env.externalBalance = extBalances;
	env.histData = hsf->create(id);
	env.spread_gen = SpreadRegister::getInstance().create(tcfg.spread_id, tcfg.spread_config);
	env.state_storage = sf->create(id);
	env.statsvc = std::make_unique<Stats2Report>(id, rpt, perfMod);
	env.strategy = StrategyRegister::getInstance().create(tcfg.spread_id, tcfg.spread_config);
	env.walletDB = prepared_walletDB;
	PTrader trd = PTrader::make(tcfg, std::move(env));
	prepared.emplace(std::string(id), trd);

}

void Traders::commit_add() {
	for (auto &k: traders) {
		if (prepared.find(k.first) == prepared.end()) {
			auto trlk = k.second.lock();
			trlk->stop();
			trlk->erase_state();
		}
	}
	rpt.lock()->clear();
	traders = std::move(prepared);
	for (auto &k: traders) {
		auto trlk = k.second.lock();
		try {
			trlk->init();
		} catch (...) {
			trlk->report_exception();
		}
	}
}

#if 0


#include "traders.h"

#include <set>
#include "../imtjson/src/imtjson/object.h"
#include "../shared/countdown.h"
#include "../shared/logOutput.h"
#include "simulator.h"

#include "ext_stockapi.h"

using ondra_shared::Countdown;
using ondra_shared::logError;
NamedMTrader::NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, const WalletCfg &wcfg, Config cfg, std::string &&name)
		:MTrader(sel, std::move(storage), std::move(statsvc), wcfg, cfg), ident(std::move(name)) {
}

void NamedMTrader::perform(bool manually) {
	using namespace ondra_shared;
	LogObject lg(ident);
	LogObject::Swap swap(lg);
	try {
		MTrader::perform(manually);
	} catch (std::exception &e) {
		logError("$1", e.what());
	}
}




Traders::Traders(ondra_shared::Scheduler sch,
		const ondra_shared::IniConfig::Section &ini,
		PStorageFactory &sf,
		const PReport &rpt,
		const PPerfModule &perfMod,
		std::string iconPath,
		int brk_timeout)

:
sf(sf)
,rpt(rpt)
,perfMod(perfMod)
,iconPath(iconPath)
{
	stockSelector.loadBrokers(ini, false, brk_timeout);
	wcfg.walletDB = PWalletDB::make();
	wcfg.externalBalance = wcfg.externalBalance.make();
	wcfg.balanceCache = wcfg.balanceCache.make();
	wcfg.accumDB = wcfg.accumDB.make();
	wcfg.conflicts = wcfg.conflicts.make();
}

void Traders::clear() {
	traders.clear();
	stockSelector.clear();
	wcfg.walletDB = wcfg.walletDB.make();
	wcfg.accumDB = wcfg.accumDB.make();
	wcfg.conflicts = wcfg.conflicts.make();
}

json::Value Traders::getUtilization(std::size_t lastUpdate) const {
	json::Object res;
	json::Object ids;
	json::Array updated;
	std::size_t lastTime = 0;
	for (const auto &x: utilization) {
		ids.set(x.first, x.second.first);
		if (x.second.second > lastUpdate) updated.push_back(x.first);
		lastTime = std::max(lastTime, x.second.second);
	}
	res.set("traders", ids);
	res.set("reset",reset_time);
	res.set("updated", updated);
	res.set("last_update", lastTime);
	return res;
}

void Traders::initExternalAssets(json::Value config) {
	wcfg.externalBalance.lock()->load(config);
}


void Traders::addTrader(const MTrader::Config &mcfg ,ondra_shared::StrViewA n) {
	using namespace ondra_shared;

	LogObject lg(n);
	LogObject::Swap swp(lg);
	try {
		logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
		if (stockSelector.checkBrokerSubaccount(mcfg.broker)) {

			auto storage = sf->create(n);
			if (!mcfg.paper_trading_src_state.empty()) {
				json::Value out = storage->load();
				if (!out.defined()) {
					auto storage2 = sf->create(mcfg.paper_trading_src_state);
					json::Value out = storage2->load();
					if (out.defined()) {
						storage->store(out);
					}
				}
			}

			auto t = SharedObject<NamedMTrader>::make(stockSelector, std::move(storage),
				std::make_unique<StatsSvc>(n, rpt, perfMod), wcfg, mcfg, n);
			auto lt = t.lock();
			try {
				lt->init();
			} catch (...) {
				//ignore exception now
			}
			traders.insert(std::pair(StrViewA(lt->ident), std::move(t)));
		} else {
			throw std::runtime_error("Unable to load broker");
		}

	} catch (const std::exception &e) {
		logFatal("Error: $1", e.what());
		throw std::runtime_error(std::string("Unable to initialize trader: ").append(n).append(" - ").append(e.what()));
	}

}


void Traders::removeTrader(ondra_shared::StrViewA n, bool including_state) {
	auto t = find(n).lock();
	if (t != nullptr) {
		if (including_state) {
			//stop trader
			t->stop();
			//perform while stop cancels all orders
			t->perform(true);
			//drop state
			t->dropState();
			//delete utilization stats
			utilization.erase(n);
			//now we can erase
		}
		traders.erase(n);
	}
}

void Traders::resetBroker(const PStockApi &api, const std::chrono::system_clock::time_point &now) {
	try {
		api->reset(now);
	} catch (std::exception &e) {
		logError("Exception when RESET: $1", e.what());
	}
}

void Traders::report_util(std::string_view ident, double ms) {
	utilization[std::string(ident)] = std::pair<double,std::size_t>{ms,
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
}

void Traders::resetBrokers() {
	auto t1 = std::chrono::system_clock::now();
	for (const auto &t: traders) {
		auto brk = t.second.lock_shared()->getBroker();
		resetBroker(brk, t1);
	}
	stockSelector.housekeepingIdle(t1);
	auto t2 = std::chrono::system_clock::now();
	reset_time = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count();
}


Traders::TMap::const_iterator Traders::begin() const {
	return traders.begin();
}

Traders::TMap::const_iterator Traders::end() const {
	return traders.end();
}

SharedObject<NamedMTrader> Traders::find(std::string_view id) const {
	auto iter = traders.find(id);
	if (iter == traders.end()) return nullptr;
	else return iter->second;
}

#endif
