/*
 * walletDB.cpp
 *
 *  Created on: 26. 9. 2020
 *      Author: ondra
 */

#include "walletDB.h"

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
	while (iter != iend) {
		if (iter->first.traderUID != key.traderUID) {
			otherTraders+=iter->second;
		} else {
			thisTrader+=iter->second;;
		}
		++iter;
	}
	return Allocation{thisTrader, otherTraders};
}

void WalletDB::clear() {
	allocTable.clear();
}

