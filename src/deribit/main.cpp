/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <rpc/rpcServer.h>
#include <imtjson/operations.h>
#include "shared/toString.h"
#include "config.h"
#include "proxy.h"
#include "../main/istockapi.h"
#include <cmath>
#include <ctime>

#include "../brokers/api.h"
#include <imtjson/stringValue.h>
#include "../shared/linear_map.h"
#include "../shared/iterator_stream.h"
#include "../brokers/orderdatadb.h"
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>
#include "../main/sgn.h"

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy &px;

	Interface(Proxy &cm):px(cm) {}


	virtual double getBalance(const std::string_view & symb) override;
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) override;
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


	ondra_shared::linear_map<std::string, double, std::less<std::string_view> > tick_cache;

};




int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "No config given, terminated" << std::endl;
		return 1;
	}

	try {

		ondra_shared::IniConfig ini;

		ini.load(argv[1]);

		Config cfg = load(ini["api"]);
		Proxy proxy(cfg);


		Interface ifc(proxy);

		ifc.dispatch();


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

inline double Interface::getBalance(const std::string_view &symb) {
	if (symb.empty()) return 0;
	if (symb[0] == '$') {
		auto instrument = symb.substr(1);
		auto response = px.request("private/get_position", Object
				("instrument_name",instrument),true);
		return -response["size"].getNumber();
	} else {
		auto response = px.request("private/get_account_summary",Object
			("currency",symb),true);
		return response["balance"].getNumber();
	}
}

inline Interface::TradeHistory Interface::getTrades(json::Value lastId,
		std::uintptr_t fromTime, const std::string_view &pair) {
	auto resp = px.request("private/get_user_trades_by_instrument",Object
			("instrument_name",pair)
			("sorting","asc")
			("start_seq", lastId),true);

	resp = resp["trades"];
	if (resp[0]["trade_seq"] == lastId) {
		resp = resp.slice(1);
	}

	return mapJSON(resp, [&](Value itm){
		double amount = itm["amount"].getNumber();
		double price = 1.0/itm["price"].getNumber();
		auto dir = itm["direction"].getString();
		if (dir == "buy") amount = -amount;
		double fee = itm["fee"].getNumber();
		double eff_price = price;
		if (fee > 0) {
			eff_price += -price/amount;
		}
		return Trade{
			itm["trade_seq"],
			itm["timestamp"].getUInt(),
			amount,
			price,
			amount,
			eff_price
		};
	},TradeHistory());

}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &par) {
	auto resp = px.request("private/get_open_orders_by_instrument",Object
			("instrument_name", par), true);

	return mapJSON(resp, [&](Value v){

		Value client_id;
		StrViewA label = v["label"].getString();
		if (!label.empty()) {try {
				client_id = Value::fromString(label);
			} catch (...) {

			}
		}
		double size = v["amount"].getNumber() - v["filled_amount"].getNumber();
		if (v["direction"].getString() == "buy") size = -size;

		return Order {
			v["order_id"],
			client_id,
			size,
			1.0/v["price"].getNumber()
		};

	}, Orders());

}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	auto response = px.request("public/ticker", Object
			("instrument_name",pair), false);
	return {
		1.0/response["best_ask_price"].getNumber(),
		1.0/response["best_bid_price"].getNumber(),
		1.0/response["last_price"].getNumber(),
		response["timestamp"].getUInt()
	};
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {
		auto response = px.request("private/cancel",Object
				("order_id",replaceId),true);
		double remain = (response["amount"].getNumber() - response["filled_amount"].getNumber())*1.00001;
		if (replaceSize > remain) return nullptr;
	}
	if (size == 0) return nullptr;

	std::string_view method  = size>0?"private/sell":"private/buy";
	double amount = std::fabs(size);
	double tick_size;
	{
		auto tick_iter = tick_cache.find(pair);
		if (tick_iter == tick_cache.end()) {
			getMarketInfo(pair);
			tick_iter = tick_cache.find(pair);
		}
		tick_size = tick_iter->second;
	}
	double adj_price = 1.0/price;
	adj_price = std::round(adj_price / tick_size) * tick_size;

	auto resp = px.request(method,Object
			("instrument_name", pair)
			("amount", amount)
			("type", "limit")
			("label", clientId.stringify())
			("price", adj_price)
			("post_only", true), true);

	return resp["order"]["order_id"];
}

inline bool Interface::reset() {
	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	auto csize = px.request("public/get_contract_size", Object
			("instrument_name", pair),false);
	auto currencies = px.request("public/get_currencies", Object(),false);
	Value instrument;
	for (Value c: currencies) {
		Value sign = c["currency"];
		auto instrs = px.request("public/get_instruments", Object
				("currency",sign)
				("kind","future")
				("expired",false),false);
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

	tick_cache[pair] = instrument["tick_size"].getNumber();
	return {
		std::string("$").append(pair),
		instrument["base_currency"].getString(),
		csize["contract_size"].getNumber(),
		0,
		csize["contract_size"].getNumber(),
		0,
		0
	};


}

inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

inline std::vector<std::string> Interface::getAllPairs() {
	std::vector<std::string>  resp;
	auto currencies = px.request("public/get_currencies", Object(),false);
	for (Value c: currencies) {
		Value sign = c["currency"];
		auto instrs = px.request("public/get_instruments", Object
				("currency",sign)
				("kind","future")
				("expired",false),false);
		for (Value i: instrs) {
			resp.push_back(i["instrument_name"].getString());
		}
	}
	return resp;
}
