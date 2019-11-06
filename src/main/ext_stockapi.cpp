/*
 * ext_stockapi.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */




#include "ext_stockapi.h"

#include <imtjson/object.h>
#include <imtjson/binary.h>
#include <fstream>
#include <set>

#include "../shared/finally.h"
using namespace ondra_shared;



ExtStockApi::ExtStockApi(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline)
:AbstractExtern(workingDir, name, cmdline) {
}



double ExtStockApi::getBalance(const std::string_view & symb) {
	return jsonRequestExchange("getBalance",StrViewA(symb)).getNumber();

}


ExtStockApi::TradesSync ExtStockApi::syncTrades(json::Value lastId, const std::string_view & pair) {
	auto r = jsonRequestExchange("syncTrades",json::Object
			("lastId",lastId)
			("pair",StrViewA(pair)));
	TradeHistory  th;
	for (json::Value v: r["trades"]) th.push_back(Trade::fromJSON(v));
	return TradesSync {
		th, r["lastId"]
	};
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
		resp["timestamp"].getUIntLong(),
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
	res.simulator= v["simulator"].getBool();
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

ExtStockApi::BrokerInfo ExtStockApi::getBrokerInfo()  {

	try {
		auto resp = jsonRequestExchange("getBrokerInfo", json::Value());
		return BrokerInfo {
			resp["trading_enabled"].getBool(),
			this->name,
			resp["name"].getString(),
			resp["url"].getString(),
			resp["version"].getString(),
			resp["licence"].getString(),
			StrViewA(resp["favicon"].getBinary()),
			resp["settings"].getBool()
		};
	} catch (IStockApi::Exception &) {
		return BrokerInfo {
			true,
			this->name,
			this->name,
		};
	}

}

void ExtStockApi::setApiKey(json::Value keyData) {
	jsonRequestExchange("setApiKey",keyData);
}

json::Value ExtStockApi::getApiKeyFields() const {
	return const_cast<ExtStockApi *>(this)->jsonRequestExchange("getApiKeyFields",json::Value());
}

json::Value ExtStockApi::getSettings(const std::string_view & pairHint) const {
	return const_cast<ExtStockApi *>(this)->jsonRequestExchange("getSettings",json::Value(pairHint));
}

void ExtStockApi::setSettings(json::Value v) {
	jsonRequestExchange("setSettings", v);
}




void ExtStockApi::saveIconToDisk(const std::string &path) const {
	Sync _(lock);

	static std::set<std::string> files;
	auto clean_call = []{
			for (auto &&k: files) std::remove(k.c_str());
	};
	static ondra_shared::FinallyImpl<decltype(clean_call)> finally(std::move(clean_call));

	std::string name =getIconName();
	std::string fullpath = path+"/"+name;
	if (files.find(fullpath) == files.end()) {
		std::ofstream f(fullpath, std::ios::out|std::ios::trunc|std::ios::binary);
		BrokerInfo binfo = const_cast<ExtStockApi *>(this)->getBrokerInfo();
		json::Binary b = json::base64->decodeBinaryValue(binfo.favicon).getBinary(json::base64);
		f.write(reinterpret_cast<const char *>(b.data), b.length);
		files.insert(fullpath);
	}
}

std::string ExtStockApi::getIconName() const {
	return name+".png";
}
