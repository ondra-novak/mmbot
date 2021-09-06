/*
 * structs.h
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */

#ifndef SRC_BITFINEX_STRUCTS_H_
#define SRC_BITFINEX_STRUCTS_H_
#include <imtjson/string.h>
#include <shared/linear_map.h>

class HTTPJson;



struct PairInfo {
	using string = json::String;

	string symbol, asset, currency;
	string tsymbol;
	double min_size, max_size;
	double leverage;
};

using PairList = ondra_shared::linear_map<std::string_view, PairInfo>;

PairList readPairs(json::Value data);

using Wallet = ondra_shared::linear_map<json::String, double>;
using MarginBalance = ondra_shared::linear_map<json::String, double>;
using Positions = ondra_shared::linear_map<json::String, double>;

Wallet readWallet(json::Value data);
MarginBalance readMarginBalance(json::Value data);
Positions readPositions(json::Value data);

#endif /* SRC_BITFINEX_STRUCTS_H_ */
