/*
 * walletDB.cpp
 *
 *  Created on: 26. 9. 2020
 *      Author: ondra
 */

#include "walletDB.h"

#include <cmath>
#include "../imtjson/src/imtjson/value.h"
WalletDB::WalletDB() {
	// TODO Auto-generated constructor stub
}

bool WalletDB::KeyLess::operator ()(const KeyQuery &key1, const KeyQuery &key2) const {
	auto c = key1.broker.compare(key2.broker);
	if (c != 0) return c < 0;
	c = key1.symbol.compare(key2.symbol);
	if (c != 0) return c < 0;
	c = key1.wallet.compare(key2.wallet);
	if (c != 0) return c < 0;
	return key1.traderUID < key2.traderUID;
}

void WalletDB::alloc(Key &&key, double allocation) {
	if (allocation<0 || !std::isfinite(allocation)) allocation = 0;
	auto iter = allocTable.find(key);
	if (iter == allocTable.end()) {
		allocTable.emplace_hint(iter, std::move(key), allocation);
	} else {
		if (allocation == 0) {
			allocTable.erase(iter);
		} else {
			iter->second = allocation;
		}
	}
}

WalletDB::Allocation WalletDB::query(const KeyQuery &key) const {
	KeyQuery start = key, end = key;
	start.traderUID = 0;
	end.traderUID = std::numeric_limits<std::size_t>::max();
	auto iter = allocTable.lower_bound(start);
	auto iend = allocTable.upper_bound(end);
	double otherTraders = 0;
	double thisTrader = 0;
	unsigned int count = 0;
	while (iter != iend) {
		if (iter->first.traderUID != key.traderUID) {
			otherTraders+=iter->second;
			++count;
		} else {
			thisTrader+=iter->second;;
		}
		++iter;
	}
	return Allocation{thisTrader, otherTraders, count};
}

double WalletDB::adjBalance(const KeyQuery &key, double balance) const {
	if (balance < 0) balance = 0;
	auto r = query(key);
	double total = r.otherTraders+r.thisTrader;
	if (balance<total) {
		if (total == 0 || (r.thisTrader == 0 && std::abs(std::log(balance/total))<1e-20)) {
			return std::max(0.0,balance/(r.traders+1));
		} else {
			double part = r.thisTrader/total;
			return std::max(0.0,part * balance);
		}
	} else {
		return std::max(0.0,balance - r.otherTraders);
	}
}

void WalletDB::clear() {
	allocTable.clear();
}

json::Value WalletDB::dumpJSON() const {
	return json::Value(json::array, allocTable.begin(), allocTable.end(), [](const AllocTable::value_type &itm){
		return json::Value({
			itm.first.broker,
			itm.first.wallet,
			itm.first.symbol,
			itm.first.traderUID,
			itm.second
		});
	});
}


double BalanceMap::get(const std::string_view &broker, const std::string_view &wallet, const std::string_view &symbol) const {
	auto iter = table.find(Key<std::string_view>{broker, wallet, symbol});
	if (iter == table.end()) return 0;
	else return iter->second;
}

void BalanceMap::put(const std::string_view &broker, const std::string_view &wallet, const std::string_view &symbol, double val) {
	table[Key<std::string>{std::string(broker),std::string(wallet),std::string(symbol)}] = val;
}

void BalanceMap::load(json::Value map) {
	table.clear();
	for (json::Value x: map) {
		auto broker = x["broker"].getString();
		auto wallet = x["wallet"].getString();
		auto symbol = x["symbol"].getString();
		auto val = x["balance"].getNumber();
		put(broker, wallet, symbol, val);
	}
}

json::Value BalanceMap::dump() const {
	return json::Value(json::array, table.begin(), table.end(),[](const Table::value_type &x){
		return json::Value(json::object,{
				json::Value("broker", x.first.broker),
				json::Value("wallet", x.first.wallet),
				json::Value("symbol", x.first.symbol),
				json::Value("balance", x.second)
		});
	});
}

double WalletDB::adjAssets(const KeyQuery &key, double assets) const {
	if (assets < 0) return assets;
	auto r = query(key);
	double total = r.otherTraders+r.thisTrader;
	if (total == 0) {
		return std::max(0.0,assets/(r.traders+1));
	} else {
		double part = r.thisTrader/total;
		return std::max(0.0,part * assets);
	}
}

std::vector<WalletDB::AggrItem> WalletDB::getAggregated() const {
	if (allocTable.empty()) return {};
	auto beg = allocTable.begin();
	AggrItem itm {beg->first.broker, beg->first.wallet, beg->first.symbol, 0};
	std::vector<AggrItem> r;

	for (const auto &x: allocTable) {
		if (x.first.broker == itm.broker && x.first.wallet == itm.wallet && x.first.symbol == itm.symbol) {
			itm.val+=x.second;
		} else {
			r.push_back(itm);
			itm.broker = x.first.broker;
			itm.symbol = x.first.symbol;
			itm.wallet = x.first.wallet;
			itm.val = x.second;
		}
	}
	r.push_back(itm);
	return r;
}
