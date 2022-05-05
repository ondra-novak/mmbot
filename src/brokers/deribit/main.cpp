/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <imtjson/operations.h>
#include "shared/toString.h"
#include "proxy.h"
#include <cmath>
#include <ctime>

#include "../api.h"
#include <imtjson/stringValue.h>
#include <shared/linear_map.h>
#include <shared/iterator_stream.h>
#include "../orderdatadb.h"
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy px;

	Interface(const std::string &path):AbstractBrokerAPI(path, {
			Object({
				{"name","key"},
				{"label","Key"},
				{"type","string"}
			}),
			Object({
				{"name","secret"},
				{"label","Secret"},
				{"type","string"}
			}),
			Object({
				{"name","scopes"},
				{"label","Scopes"},
				{"type","string"},
				{"default","session:apiconsole"}
			}),
			Object({
				{"name","server"},
				{"label","Server"},
				{"type","enum"},
				{"options",Object({
					{"main","www.deribit.com"},
					{"test","test.deribit.com"}
				})},
				{"default","main"}
			})
			})
	{}


	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par)override;
	virtual Ticker getTicker(const std::string_view & piar)override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId,
			json::Value replaceId,
			double replaceSize)override;
	virtual bool reset()override;
	virtual MarketInfo getMarketInfo(const std::string_view & pair)override;
	virtual double getFees(const std::string_view &pair)override;
	virtual std::vector<std::string> getAllPairs()override;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;
	virtual Interface *createSubaccount(const std::string &path) override {
		return new Interface(path);
	}
	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
	virtual json::Value getWallet_direct() override;



	ondra_shared::linear_map<std::string, json::Value, std::less<std::string_view> > openOrdersCache;

};




int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Requires a signle parametr" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();

}

inline double Interface::getBalance(const std::string_view &symb, const std::string_view &pair ) {
	if (symb.empty()) return 0;
	if (symb == "CONTRACT") {
		auto instrument = pair;
		auto response = px.request("private/get_position", Object({
			{"instrument_name",instrument}}),true);
		return response["size"].getNumber();
	} else {
		auto response = px.request("private/get_account_summary",Object({
			{"currency",symb}}),true);
		return response["margin_balance"].getNumber();
	}
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId,  const std::string_view &pair) {
	auto resp = px.request("private/get_user_trades_by_instrument",Object({
		{"instrument_name",pair},
		{"sorting","asc"},
		{"count", 1000},
		{"include_old", true},
		{"start_seq", lastId.hasValue()?lastId:Value()}}),true);


	resp = resp["trades"];

	if (!lastId.hasValue()) {

		if (resp.empty()) {
			return TradesSync{ {}, Value(nullptr) };
		} else {
			return TradesSync{ {}, resp[resp.size()-1]["trade_seq"]};
		}

	} else {


		if (resp[0]["trade_seq"] == lastId) {
			resp = resp.slice(1);
		}


		auto trades = mapJSON(resp, [&](Value itm){
			double size = itm["amount"].getNumber();
			double price = itm["price"].getNumber();
			auto dir = itm["direction"].getString();
			if (dir == "sell") size = -size;
			double fee = itm["fee"].getNumber();
			double eff_price = fee>0?price * size /(fee*price + size):price;
			lastId = itm["trade_seq"];
			return Trade{
				itm["trade_seq"],
				itm["timestamp"].getUIntLong(),
				size,
				price,
				size,
				eff_price
			};
		},TradeHistory());
		return TradesSync { trades, lastId };

	}
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {
	Value &resp = openOrdersCache[pair];
	if (!resp.defined()) {
		resp = px.request("private/get_open_orders_by_instrument",Object
			({{"instrument_name", pair}}), true);
	}

	return mapJSON(resp, [&](Value v){

		Value client_id;
		auto label = v["label"].getString();
		if (!label.empty()) {try {
				client_id = Value::fromString(label);
			} catch (...) {

			}
		}
		double size = v["amount"].getNumber() - v["filled_amount"].getNumber();
		if (v["direction"].getString() == "sell") size = -size;

		double price = -1;
		Value vprice = v["price"];
		if (vprice.type() == json::number) price = vprice.getNumber();

		return Order {
			v["order_id"],
			client_id,
			size,
			price
		};

	}, Orders());

}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	auto response = px.request("public/ticker", Object
			({{"instrument_name",pair}}), false);
	return {
		response["best_bid_price"].getNumber(),
		response["best_ask_price"].getNumber(),
		response["last_price"].getNumber(),
		response["timestamp"].getUIntLong()
	};
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {

		auto response = px.request("private/cancel",Object({{"order_id",replaceId}}),true);
		double remain = (response["amount"].getNumber() - response["filled_amount"].getNumber())*1.00001;
		if (replaceSize > remain) return nullptr;
	}
	if (size == 0) return nullptr;

	std::string_view method  = size<0?"private/sell":"private/buy";
	double amount = std::fabs(size);

	auto resp = px.request(method,Object({
		{"instrument_name", pair},
		{"amount", amount},
		{"type", "limit"},
		{"label", clientId.stringify()},
		{"price", price},
		{"post_only", true}}), true);

	return resp["order"]["order_id"];
}

inline bool Interface::reset() {
	openOrdersCache.clear();
	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	auto csize = px.request("public/get_contract_size", Object
			({{"instrument_name", pair}}),false);
	auto currencies = px.request("public/get_currencies", Object(),false);
	Value instrument;
	for (Value c: currencies) {
		Value sign = c["currency"];
		auto instrs = px.request("public/get_instruments", Object({
			{"currency",sign},
			{"kind","future"},
			{"expired",false}}),false);
		for (Value i: instrs) {
			if (i["instrument_name"].getString() == pair) {
				instrument = i;
				break;
			}
		}
		if (instrument.defined())
			break;
	}
	if (!instrument.defined())
		throw std::runtime_error("No such symbol");

	auto bcur = instrument["base_currency"].getString();
	double leverage;
	if (bcur == "BTC") leverage=100.0;
	else if (bcur == "ETH") leverage=50.0;
	else leverage = 0;

	return {
		"CONTRACT",
		bcur,
		csize["contract_size"].getNumber(),
		0,
		csize["contract_size"].getNumber(),
		0,
		0,
		FeeScheme::currency,
		leverage,
		false,
		"USD",
		px.testnet,
		false,
		"",
		MarketType::inverted
	};


}

inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}


Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"deribit",
		"Deribit",
		"https://www.deribit.com/reg-8695.6923?q=home",
		"1.0",
		R"mit(Copyright (c) 2019 Ondřej Novák

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.)mit",
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAABlBMVEUAAAAtrpq1rIdBAAAAAXRS"
"TlMAQObYZgAAANRJREFUeAHt2jcSwzAMBVHs/S9t18r5cwa7rYNeJYkkysxWYt7BrwkQIECAAAEC"
"BAh47fpw/Gsn47XiAIA4AIgDIA6AOADiAOIA4gCIA4gDiAOIA4gDiAMQQBzAYIB33jbZ7APAvzhg"
"i/ARoCoOWBV8Bqg4YE3wHaDigBoTwIeAigNKQBiwIhAgQIAAAT6MOgHoDui+LiAKyK8NWanL/gBZ"
"ACQBbOVGZe+9Yrfru58ZdT837H523Hx+oPcMSe85oqogIDlL5kCjAAECBAgQIECAmR3sB12WHA4r"
"Mg73AAAAAElFTkSuQmCC",false,true
	};
}

inline std::vector<std::string> Interface::getAllPairs() {
	std::vector<std::string>  resp;
	auto currencies = px.request("public/get_currencies", Object(),false);
	for (Value c: currencies) {
		Value sign = c["currency"];
		auto instrs = px.request("public/get_instruments", Object({
			{"currency",sign},
			{"kind","future"},
			{"expired",false}}),false);
		for (Value i: instrs) {
			resp.push_back(i["instrument_name"].getString());
		}
	}
	return resp;
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	px.setTestnet(keyData["server"].getString() == "test");
	px.privKey = keyData["secret"].getString();
	px.pubKey = keyData["key"].getString();
	px.scopes = keyData["scopes"].getString();
}

inline void Interface::onInit() {
	//empty
}

json::Value Interface::getWallet_direct() {
	auto currencies = px.request("public/get_currencies", Object(),false);
	Object w;

	for (Value c: currencies) {
		Value symb = c["currency"];
		auto response = px.request("private/get_account_summary",Object
				({{"currency",symb}}),true);
		double n = response["margin_balance"].getNumber();
		if (n) {
			w.set(symb.getString(),n);
		}
	}
	return Object({{"default",w}});
}
