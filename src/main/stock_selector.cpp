/*
 * stock_selector.cpp
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include "stock_selector.h"

#include <memory>

#include "ext_stockapi.h"
#include "simulator.h"
void StockSelector::loadBrokers(const ondra_shared::IniConfig::Section &ini) {
	auto brk_timeout = ini["timeout"].getInt(10000);
	std::vector<StockMarketMap::value_type> data;
	for (auto &&def: ini) {
		std::string_view name = def.first.getView();
		if (name == "timeout") continue;
		std::string_view cmdline = def.second.getString();
		if (!cmdline.empty()) {
			std::string_view workDir = def.second.getCurPath();
			data.push_back(StockMarketMap::value_type(name,std::make_shared<ExtStockApi>(workDir, name, cmdline, brk_timeout)));
		}
	}
	StockMarketMap map(std::move(data));
	stock_markets.swap(map);
	appendSimulator();
}



void StockSelector::appendSimulator() {
	PStockApi sim = std::make_shared<Simulator>(this);
	stock_markets.emplace(dynamic_cast<IBrokerControl *>(sim.get())->getBrokerInfo().name, sim);
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
		temp_markets.lock()->emplace(name, PStockApi(nk));
	}
	return true;
}



void StockSelector::enum_brokers(AbstractBrokerList::EnumFn &&fn)  const {
	for(auto &&x: stock_markets) {
		fn(x.first, x.second);
	}
	auto tmp = temp_markets.lock_shared();
	for (auto &&x: *tmp) {
		fn(x.first, x.second);
	}
}
void StockSelector::clear() {
	stock_markets.clear();
}

StockSelector::StockSelector() {
	temp_markets = temp_markets.make();
}

void StockSelector::housekeepingIdle(const std::chrono::system_clock::time_point &tp) {
	std::vector<std::string> todel;
	for (auto &&x: stock_markets) {
		auto *c = dynamic_cast<IBrokerInstanceControl *>(x.second.get());
		if (c && c->isIdle(tp))
			c->unload();
	}
	for (auto &&x: *temp_markets.lock_shared()) {
		auto *c = dynamic_cast<IBrokerInstanceControl *>(x.second.get());
		if (c && c->isIdle(tp)) {
			c->unload();
			todel.push_back(x.first);
		}
	}
	for (const auto &x: todel)
		temp_markets.lock()->erase(x);
}

PStockApi StockSelector::get_broker(const std::string_view &name) {
	auto f = stock_markets.find(name);
	if (f != stock_markets.end()) return f->second;
	auto tmp = temp_markets.lock_shared();
	auto g = tmp->find(name);
	if (g != tmp->end()) return f->second;
	auto n = name.rfind("~");
	if (n == name.npos) return nullptr;
	std::string_view baseName = name.substr(0,n);
	std::string_view id = name.substr(n+1);
	for (char c : id) if (!isalnum(c)) return nullptr;
	f = stock_markets.find(baseName);
	if (f == stock_markets.end()) return nullptr;
	auto k = f->second.get();
	auto ek = dynamic_cast<IBrokerSubaccounts *>(k);
	if (ek == nullptr) return nullptr;
	auto sub = ek->createSubaccount(id);
	if (sub == nullptr) return nullptr;
	tmp.release();
	auto ret = PStockApi(sub);
	temp_markets.lock()->emplace(name, ret);
	return ret;
}

