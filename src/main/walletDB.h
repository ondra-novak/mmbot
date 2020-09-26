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

	void clear();

protected:
	AllocTable allocTable;
};

using PWalletDB = ondra_shared::SharedObject<WalletDB>;


#endif /* SRC_MAIN_WALLETDB_H_ */
