/*
 * traders.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */




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

