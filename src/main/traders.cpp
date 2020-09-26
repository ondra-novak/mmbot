/*
 * traders.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */




#include "traders.h"

#include "../shared/countdown.h"
#include "../shared/logOutput.h"
#include "ext_stockapi.h"

using ondra_shared::Countdown;
using ondra_shared::logError;
NamedMTrader::NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, PWalletDB wdb, Config cfg, std::string &&name)
		:MTrader(sel, std::move(storage), std::move(statsvc), wdb, cfg), ident(std::move(name)) {
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




void StockSelector::loadBrokers(const ondra_shared::IniConfig::Section &ini, bool test, int brk_timeout) {
	std::vector<StockMarketMap::value_type> data;
	for (auto &&def: ini) {
		ondra_shared::StrViewA name = def.first;
		ondra_shared::StrViewA cmdline = def.second.getString();
		if (!cmdline.empty()) {
			ondra_shared::StrViewA workDir = def.second.getCurPath();
			data.push_back(StockMarketMap::value_type(name,std::make_shared<ExtStockApi>(workDir, name, cmdline, brk_timeout)));
		}
	}
	StockMarketMap map(std::move(data));
	stock_markets.swap(map);
}

bool StockSelector::checkBrokerSubaccount(const std::string &name) {
	auto f = stock_markets.find(name);
	if (f == stock_markets.end()) {
		auto n = name.rfind("~");
		if (n == name.npos) return false;
		std::string baseName = name.substr(0,n);
		std::string id = name.substr(n+1);
		f = stock_markets.find(baseName);
		if (f == stock_markets.end()) return false;
		IStockApi *k = f->second.get();
		IBrokerSubaccounts *ek = dynamic_cast<IBrokerSubaccounts *>(k);
		if (ek == nullptr) return false;
		IStockApi *nk = ek->createSubaccount(id);
		stock_markets.emplace(name, PStockApi(nk));
	}
	return true;
}

PStockApi StockSelector::getStock(const std::string_view &stockName) const {
	auto f = stock_markets.find(stockName);
	if (f == stock_markets.cend()) return nullptr;
	return f->second;
}
/*
void StockSelector::addStockMarket(ondra_shared::StrViewA name, PStockApi &&market) {
	stock_markets.insert(std::pair(name,std::move(market)));
}*/

void StockSelector::forEachStock(EnumFn fn)  const {
	for(auto &&x: stock_markets) {
		fn(x.first, x.second);
	}
}
void StockSelector::clear() {
	stock_markets.clear();
}

Traders::Traders(ondra_shared::Scheduler sch,
		const ondra_shared::IniConfig::Section &ini,
		bool test,
		PStorageFactory &sf,
		const PReport &rpt,
		const PPerfModule &perfMod,
		std::string iconPath,
		int brk_timeout)

:
test(test)
,sf(sf)
,rpt(rpt)
,perfMod(perfMod)
,iconPath(iconPath)
{
	stockSelector.loadBrokers(ini, test, brk_timeout);
	walletDB = PWalletDB::make();
}

void Traders::clear() {
	traders.clear();
	stockSelector.clear();
	walletDB.lock()->clear();
}

void Traders::loadIcon(MTrader &t) {
	PStockApi api = t.getBroker();
	const IBrokerIcon *bicon = dynamic_cast<const IBrokerIcon*>(api.get());
	if (bicon)
		bicon->saveIconToDisk(iconPath);
}

void Traders::addTrader(const MTrader::Config &mcfg ,ondra_shared::StrViewA n) {
	using namespace ondra_shared;

	LogObject lg(n);
	LogObject::Swap swp(lg);
	try {
		logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
		if (stockSelector.checkBrokerSubaccount(mcfg.broker)) {
			auto t = SharedObject<NamedMTrader>::make(stockSelector, sf->create(n),
				std::make_unique<StatsSvc>(n, rpt, perfMod), walletDB, mcfg, n);
			auto lt = t.lock();
			loadIcon(*lt);
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
			//now we can erase
		}
		traders.erase(n);
	}
}

static void resetBroker(const PStockApi &api) {
	AbstractExtern *extr = dynamic_cast<AbstractExtern *>(api.get());
	if (extr) extr->housekeeping(5);
	try {
		api->reset();
	} catch (std::exception &e) {
		logError("Exception when RESET: $1", e.what());
	}
}

void Traders::resetBrokers() {
	stockSelector.forEachStock([](json::StrViewA, const PStockApi &api) {
		resetBroker(api);
	});
}

/*void Traders::runTraders(bool manually) {

	if (worker.defined()) {
		Countdown cdn;
		stockSelector.forEachStock([&cdn,worker=this->worker](json::StrViewA, IStockApi&api) {
			cdn.inc();
			worker >> [&cdn,&api] {
				resetBroker(api);
				cdn.dec();
			};
		});
		for (auto &&t : traders) {
			cdn.inc();
			worker >> [t,manually,&cdn]() mutable {
				auto lt = t.second.lock();
				try {lt->perform(manually);} catch (...) {}
				cdn.dec();
			};
		}
		cdn.wait();
	} else {
		resetBrokers();
		for (auto &&t : traders) {
			auto lt = t.second.lock();
			lt->perform(manually);
		}
	}
}
*/

Traders::TMap::const_iterator Traders::begin() const {
	return traders.begin();
}

Traders::TMap::const_iterator Traders::end() const {
	return traders.end();
}

SharedObject<NamedMTrader> Traders::find(json::StrViewA id) const {
	auto iter = traders.find(id);
	if (iter == traders.end()) return nullptr;
	else return iter->second;
}

void Traders::loadIcons(const std::string &path) {
	for (auto &&t: traders) {
		auto lt = t.second.lock_shared();
		PStockApi api = lt->getBroker();
		const IBrokerIcon *bicon = dynamic_cast<const IBrokerIcon *>(api.get());
		if (bicon) bicon->saveIconToDisk(path);
	}
}

void StockSelector::eraseSubaccounts() {
	for (auto iter = stock_markets.begin(); iter != stock_markets.end();) {
		IBrokerSubaccounts *sb = dynamic_cast<IBrokerSubaccounts *>(iter->second.get());
		if (sb && sb->isSubaccount()) iter = stock_markets.erase(iter); else ++iter;
	}
}
