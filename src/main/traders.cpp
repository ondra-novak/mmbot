/*
 * traders.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */




#include "traders.h"

#include "ext_stockapi.h"
NamedMTrader::NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, Config cfg, std::string &&name)
		:MTrader(sel, std::move(storage), std::move(statsvc), cfg), ident(std::move(name)) {
}

bool NamedMTrader::perform() {
	using namespace ondra_shared;
	LogObject lg(ident);
	LogObject::Swap swap(lg);
	try {
		return MTrader::perform();
	} catch (std::exception &e) {
		logError("$1", e.what());
		return false;
	}
}




void StockSelector::loadStockMarkets(const ondra_shared::IniConfig::Section &ini, bool test) {
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
IStockApi *StockSelector::getStock(const std::string_view &stockName) const {
	auto f = stock_markets.find(stockName);
	if (f == stock_markets.cend()) return nullptr;
	return f->second.get();
}
void StockSelector::addStockMarket(ondra_shared::StrViewA name, PStockApi &&market) {
	stock_markets.insert(std::pair(name,std::move(market)));
}

void StockSelector::forEachStock(EnumFn fn)  const {
	for(auto &&x: stock_markets) {
		fn(x.first, *x.second);
	}
}
void StockSelector::clear() {
	stock_markets.clear();
}

Traders::Traders(ondra_shared::Scheduler sch,
		const ondra_shared::IniConfig::Section &ini,
		bool test,
		int spread_calc_interval,
		StorageFactory &sf,
		Report &rpt)

:aq ( new ActionQueue(sch) )
,test(test)
,spread_calc_interval(spread_calc_interval)
,sf(sf)
,rpt(rpt)
{
	stockSelector.loadStockMarkets(ini, test);
}

void Traders::clear() {
	traders.clear();
	stockSelector.clear();
}


void Traders::addTrader(const MTrader::Config &mcfg ,ondra_shared::StrViewA n) {
	using namespace ondra_shared;

	LogObject lg(n);
	LogObject::Swap swp(lg);
	try {
		logProgress("Started trader $1 (for $2)", n, mcfg.pairsymb);
		auto t = std::make_unique<NamedMTrader>(stockSelector, sf.create(n),
				std::make_unique<StatsSvc>([aq = this->aq](auto &&fn) {
						aq->push(std::move(fn));
				}, n, rpt, spread_calc_interval),
				mcfg, n);
		traders.insert(std::pair(StrViewA(t->ident), std::move(t)));
	} catch (const std::exception &e) {
		logFatal("Error: $1", e.what());
		throw std::runtime_error(std::string("Unable to initialize trader: ").append(n).append(" - ").append(e.what()));
	}

}

void Traders::loadTraders(const ondra_shared::IniConfig &ini, ondra_shared::StrViewA names) {
	std::vector<json::StrViewA> nv;

	auto nspl = names.split(" ");
	while (!!nspl) {
		json::StrViewA x = nspl();
		if (!x.empty()) nv.push_back(x);
	}

	for (auto n: nv) {
		addTrader(MTrader::load(ini[n], test), n);
	}
}

void Traders::removeTrader(ondra_shared::StrViewA n, bool including_state) {
	NamedMTrader *t = find(n);
	if (t) {
		if (including_state) {
			//stop trader
			t->stop();
			//perform while stop cancels all orders
			t->perform();
			//drop state
			t->dropState();
			//now we can erase
		}
		traders.erase(n);
	}
}

bool Traders::runTraders() {
	stockSelector.forEachStock([](json::StrViewA, IStockApi&api) {
		api.reset();
	});

	bool hit = false;
	for (auto &&t : traders) {
		bool h = t.second->perform();
		hit |= h;
	}
	return hit;
}

Traders::TMap::const_iterator Traders::begin() const {
	return traders.begin();
}

Traders::TMap::const_iterator Traders::end() const {
	return traders.end();
}

NamedMTrader *Traders::find(json::StrViewA id) const {
	auto iter = traders.find(id);
	if (iter == traders.end()) return nullptr;
	else return iter->second.get();
}
