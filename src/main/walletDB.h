/*
 * walletDB.h
 *
 *  Created on: 26. 9. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_WALLETDB_H_
#define SRC_MAIN_WALLETDB_H_
#include <map>

#include <shared/shared_object.h>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace json {
	class Value;
}

class WalletDB {
public:
	WalletDB();

	struct Key {
		std::string broker;
		std::string wallet;
		std::string symbol;
		std::size_t traderUID;
	};

	struct KeyQuery {
		std::string_view broker;
		std::string_view wallet;
		std::string_view symbol;
		std::size_t traderUID;

		KeyQuery(const Key &k)
				:broker(k.broker)
				,wallet(k.wallet)
				,symbol(k.symbol)
				,traderUID(k.traderUID) {}

		KeyQuery(const std::string_view &broker,
				 const std::string_view &wallet,
				 const std::string_view &symbol,
				 std::size_t traderUID)
			:broker(broker),wallet(wallet),symbol(symbol),traderUID(traderUID) {}

	};

	struct KeyLess {
		using is_transparent = void;
		bool operator()(const KeyQuery &key1, const KeyQuery &key2) const;
	};

	struct Allocation {
		double thisTrader;
		double otherTraders;
		unsigned int traders;
		double total() const {return thisTrader+otherTraders;}
	};

	using AllocTable = std::map<Key, double, KeyLess>;


	///Allocate budget for given symbol
	/**
	 * @param key specify symbol
	 * @param allocation amount of allocated
	 */
	void alloc(Key &&key, double allocation);
	///Query for amount allocated by other traders
	/**
	 * @param key specify symbol of current trader
	 * @return amount allocated by other traders
	 */
	Allocation query(const KeyQuery &key) const;

	double adjBalance(const KeyQuery &key, double balance) const;
	double adjAssets(const KeyQuery &key, double assets) const;

	void clear();

	json::Value dumpJSON() const;


	struct AggrItem {
		std::string_view broker;
		std::string_view wallet;
		std::string_view symbol;
		double val = 0;
	};

	std::vector<AggrItem> getAggregated() const;


protected:
	AllocTable allocTable;
};

using PWalletDB = ondra_shared::SharedObject<WalletDB>;

class BalanceMap {
public:

	template<typename Z>
	struct Key {
		Z broker;
		Z wallet;
		Z symbol;
	};


	struct ItemCmp{
		template<typename A, typename B>
		std::size_t operator()(const Key<A> &a, const Key<B> &b) const {
			if (std::string_view(a.broker) == std::string_view(b.broker)) {
				if (std::string_view(a.wallet) == std::string_view(b.wallet)) {
					return std::string_view(a.symbol) < std::string_view(b.symbol);
				} else {
					return std::string_view(a.wallet) < std::string_view(b.wallet);
				}
			} else {
				return std::string_view(a.broker) == std::string_view(b.broker);
			}
		}
		using is_transparent = void;
	};


	using Table = std::map<Key<std::string>, double, ItemCmp>;


	double get(const std::string_view &broker, const std::string_view &wallet, const std::string_view &symbol) const;
	void put(const std::string_view &broker, const std::string_view &wallet, const std::string_view &symbol, double val);
	void load(json::Value map);
	json::Value dump() const;

protected:
	Table table;

};

using PBalanceMap= ondra_shared::SharedObject<BalanceMap>;

#endif /* SRC_MAIN_WALLETDB_H_ */
