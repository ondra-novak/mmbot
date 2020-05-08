/*
 * structs.cpp
 *
 *  Created on: 6. 5. 2020
 *      Author: ondra
 */


#include "imtjson/value.h"
#include "imtjson/string.h"
#include "../brokers/httpjson.h"
#include "structs.h"


using json::String;
using json::Value;

static std::pair<String, String> crackSymbol(StrViewA symbol) {
	auto splpos = symbol.indexOf(":");
	if (splpos != symbol.npos) {
		return {
			symbol.substr(0,splpos),
			symbol.substr(splpos+1)
		};
	} else if (symbol.length == 6) {
		return {
			symbol.substr(0,3),
			symbol.substr(3)
		};
	} else {
		return {"",""};
	}
}


PairList readPairs(json::Value data) {
	PairList::Set::VecT lst;
	for(Value rw: data[0]) {
		String symb = rw[0].toString();
		Value details = rw[1];
		auto asscur = crackSymbol(symb);
		if (asscur.first.empty()) continue;
		double lnfo = details[8].getNumber();
		double leverage = lnfo>0?1.0/lnfo:0;
		String id({asscur.first,"/",asscur.second});
		lst.push_back({
			id,
			{
					id,
					asscur.first,
					asscur.second,
					String({"t", symb}),
					details[3].getNumber(),
					details[4].getNumber(),
					leverage,
			}
		});
	}
	return PairList(std::move(lst));
}

Wallet readWallet(json::Value data) {
	Wallet::Set::VecT w;
	for (Value v:data) {
		if (v[0].getString()[0] == 'e') {
			w.push_back({v[1].toString(),v[2].getNumber()});
		}
	}
	return Wallet(std::move(w));
}

MarginBalance readMarginBalance(json::Value data) {
	MarginBalance::Set::VecT w;
	for (Value v:data) {
		if (v[0].getString()[0] == 's') {
			w.push_back({v[1].toString(),v[2][1].getNumber()});
		}
	}
	return MarginBalance(std::move(w));
}

Positions readPositions(json::Value data) {
	Positions::Set::VecT w;
	for (Value v:data) {
		w.push_back({v[0].toString(),v[2].getNumber()});
	}
	return Positions(std::move(w));
}
