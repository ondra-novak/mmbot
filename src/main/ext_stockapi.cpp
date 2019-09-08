/*
 * ext_stockapi.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */




#include "ext_stockapi.h"

#include <imtjson/object.h>

using namespace ondra_shared;



ExtStockApi::ExtStockApi(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline)
:AbstractExtern(workingDir, name, cmdline) {
}



double ExtStockApi::getBalance(const std::string_view & symb) {
	return jsonRequestExchange("getBalance",StrViewA(symb)).getNumber();

}


ExtStockApi::TradeHistory ExtStockApi::getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {
	auto r = jsonRequestExchange("getTrades",json::Object
			("lastId",lastId)
			("fromTime", fromTime)
			("pair",StrViewA(pair)));
	TradeHistory  th;
	for (json::Value v: r) th.push_back(Trade::fromJSON(v));
	return th;
}

ExtStockApi::Orders ExtStockApi::getOpenOrders(const std::string_view & pair) {
	Orders r;

	auto v = jsonRequestExchange("getOpenOrders",StrViewA(pair));
	for (json::Value x: v) {
		Order ord {
			x["id"],
			x["clientOrderId"],
			x["size"].getNumber(),
			x["price"].getNumber()
		};
		r.push_back(ord);
	}
	return r;
}

ExtStockApi::Ticker ExtStockApi::getTicker(const std::string_view & pair) {
	auto resp =  jsonRequestExchange("getTicker", StrViewA(pair));
	return Ticker {
		resp["bid"].getNumber(),
		resp["ask"].getNumber(),
		resp["last"].getNumber(),
		resp["timestamp"].getUInt(),
	};
}

json::Value  ExtStockApi::placeOrder(const std::string_view & pair,
		double size, double price,json::Value clientId,
		json::Value replaceId,double replaceSize) {

	return jsonRequestExchange("placeOrder",json::Object
					("pair",StrViewA(pair))
					("price",price)
					("size",size)
					("clientOrderId",clientId)
					("replaceOrderId",replaceId)
					("replaceOrderSize",replaceSize));
}


bool ExtStockApi::reset() {
	if (chldid != -1) try {
		jsonRequestExchange("reset",json::Value());
	} catch (...) {
		jsonRequestExchange("reset",json::Value());
	}
	return true;
}

ExtStockApi::MarketInfo ExtStockApi::getMarketInfo(const std::string_view & pair) {
	json::Value v = jsonRequestExchange("getInfo",StrViewA(pair));

	MarketInfo res;
	res.asset_step = v["asset_step"].getNumber();
	res.currency_step = v["currency_step"].getNumber();
	res.asset_symbol = v["asset_symbol"].getString();
	res.currency_symbol = v["currency_symbol"].getString();
	res.min_size = v["min_size"].getNumber();
	res.min_volume= v["min_volume"].getNumber();
	res.fees = v["fees"].getNumber();
	res.feeScheme = strFeeScheme[v["feeScheme"].getString()];
	res.leverage= v["leverage"].getNumber();
	res.invert_price= v["invert_price"].getBool();
	res.inverted_symbol= v["inverted_symbol"].getString();
	return res;

}

double ExtStockApi::getFees(const std::string_view& pair) {
	json::Value v = jsonRequestExchange("getFees",pair);
	return v.getNumber();

}

std::vector<std::string> ExtStockApi::getAllPairs() {
	json::Value v = jsonRequestExchange("getAllPairs", json::Value());
	std::vector<std::string> res;
	res.reserve(v.size());
	for (json::Value x: v) res.push_back(x.toString().str());
	return res;
}

void ExtStockApi::onConnect() {
	ondra_shared::LogObject lg("");
	bool debug= lg.isLogLevelEnabled(ondra_shared::LogLevel::debug);
	try {
		jsonRequestExchange("enableDebug",debug);
	} catch (IStockApi::Exception &) {

	}
}
