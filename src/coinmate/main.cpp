/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <unordered_map>

#include <rpc/rpcServer.h>
#include "../imtjson/src/imtjson/operations.h"
#include "proxy.h"
#include "../main/istockapi.h"
#include "../shared/linear_map.h"
#include <brokers/api.h>


using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy cm;

	Interface(const std::string &path)
		:AbstractBrokerAPI(path,{Object
				("name","pubKey")
				("label","Public key")
				("type", "string"),
			Object
				("name","privKey")
				("label","Private key")
				("type", "string"),
			Object
				("name","clientId")
				("label","Client ID")
				("type", "string")
		}) {}


	virtual double getBalance(const std::string_view & symb) override;
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId,
			json::Value replaceId,
			double replaceSize) override;
	virtual bool reset() override ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override ;
	virtual double getFees(const std::string_view &pair) override ;
	virtual std::vector<std::string> getAllPairs() override ;
	virtual void enable_debug(bool enable) override {cm.debug = enable;}
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override {}

	Value balanceCache;
	Value orderCache;
	Value tradeCache;
	Value all_pairs;
	bool fetch_trades = true;

	struct FeeInfo {
		double fee;
		std::chrono::system_clock::time_point expiration;
	};

	ondra_shared::linear_map<String, FeeInfo> feeMap;



	Value readTradesPerPartes(Value lastId, Value fromTime);
};

 double Interface::getBalance(const std::string_view &symb) {
	 if (!balanceCache.defined()) {
//			std::cerr << "[debug] Fetch balance: " << std::endl;
		 	balanceCache = cm.request(Proxy::POST, "balances", Value());
	 }
	 return balanceCache[symb]["balance"].getNumber();
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	cm.privKey = keyData["privKey"].getString();
	cm.pubKey = keyData["pubKey"].getString();
	cm.clientid = keyData["clientId"].getString();
	balanceCache = Value();
	orderCache = Value();
	tradeCache = Value();
	all_pairs = Value();
	fetch_trades = true;

}

static bool greaterThanId(Value tx, Value id) {
	return Value::compare(id, tx["transactionId"]) < 0;
}
static int sortById(Value tx1, Value tx2) {
	return Value::compare(tx1["transactionId"],tx2["transactionId"]);
}

Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view & pair) {

		if (!tradeCache.hasValue()) {
			json::Value args (json::object, {
					json::Value("limit",10)
			});
			Value c =  cm.request(Proxy::POST, "tradeHistory", args).sort(sortById);

			if (c.empty()) return TradesSync{ {}, nullptr };

			tradeCache = c;
			fetch_trades = false;
		}

		if (lastId.hasValue() && greaterThanId(tradeCache[0], lastId)) {

			json::Value args (json::object, {
					json::Value("sort", "ASC"),
					json::Value("lastId",lastId),
					json::Value("limit",1000)
			});
			tradeCache = cm.request(Proxy::POST, "tradeHistory", args);
			fetch_trades = false;
		}

		Value l = tradeCache[tradeCache.size()-1]["transactionId"];


		if (fetch_trades) {

			json::Value args (json::object, {
					json::Value("sort", "ASC"),
					json::Value("lastId",l),
					json::Value("limit",1000)
			});
			tradeCache = tradeCache.merge(cm.request(Proxy::POST, "tradeHistory", args));
			fetch_trades = false;
			l = tradeCache[tradeCache.size()-1]["transactionId"];
		}


		if (lastId.hasValue()) {

			Value trades = tradeCache.filter([&](Value v) {
				return v["currencyPair"] == pair && greaterThanId(v, lastId);
			});

			return TradesSync{
				mapJSON<TradeHistory> (trades, [&](Value x){
					double mlt = x["type"].getString() == "SELL"?-1:1;
					double price = x["price"].getNumber();
					double fee = x["fee"].getNumber();
					double size = x["amount"].getNumber()*mlt;
					double eff_price = price + fee/size;
					return Trade {
						x["transactionId"],
						x["createdTimestamp"].getUIntLong(),
						size,
						price,
						size,
						eff_price
					};
				}), l};

		} else {
			return TradesSync{ {}, l};
		}

}


 Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {


	 	 if (!orderCache.defined()) {
//  			 std::cerr << "[debug] Fetch orders: " << std::endl;
	 		 orderCache = cm.request(Proxy::POST, "openOrders", Value());
	 	 }

	 	 return  mapJSON<Orders>(orderCache.filter([&](Value order) {
	 		 return order["currencyPair"].getString() == pair;
	 	 }),[](Value order) {
			double mlt = order["type"].getString() == "SELL"?-1:1;
			return Order {order["id"],
						  order["clientOrderId"],
						  order["amount"].getNumber()*mlt,
						  order["price"].getNumber()
			};
		});
}

 Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	 json::Value args (json::object, {json::Value("currencyPair",pair)});
	 Value r = cm.request(Proxy::GET, "ticker", args);
	 return Ticker{
		 r["bid"].getNumber(),
		 r["ask"].getNumber(),
		 r["last"].getNumber(),
		 r["timestamp"].getUIntLong()*1000 //HACK - time is not in milliseconds
	 };
}

static const char *place[] = {"sellLimit","buyLimit"};
//static const char *replace[] = {"replaceBySellLimit","replaceByBuyLimit"};


json::Value Interface::placeOrder(const std::string_view & pair,
		double size,
		double price,
		json::Value clientId,
		json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {
		Value res = cm.request(Proxy::POST, "cancelOrderWithInfo",Object("orderId", replaceId));
		if (replaceSize && (
				res["success"].getBool() != true
				|| res["remainingAmount"].getNumber()<std::fabs(replaceSize)*0.999999)) {
				return null;
		}
	}

	const char *cmd = nullptr;

	double amount;
	const char **cmdset = place;
	if (size<0) {
		cmd = cmdset[0];
		amount = -size;
	} else if (size > 0){
		cmd = cmdset[1];
		amount = +size;
	} else {
		return null;
	}

	json::Value args(json::object,{
		json::Value("amount", amount),
		json::Value("price", price),
		json::Value("currencyPair", pair),
		json::Value("clientOrderId",clientId)
	});

	auto resp = cm.request(Proxy::POST, cmd, args);
	return resp;
}

bool Interface::reset() {
	balanceCache = Value();
	orderCache = Value();
	fetch_trades = true;
	return true;
}

std::vector<std::string> Interface::getAllPairs() {
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	return mapJSON< std::vector<std::string> >(all_pairs, [&](Value z){
		return z["name"].toString().str();
	});
}

double Interface::getFees(const std::string_view &pair) {
	if (cm.hasKey()) {
		auto now = std::chrono::system_clock::now();
		auto iter = feeMap.find(StrViewA(pair));
		if (iter == feeMap.end() || iter->second.expiration < now) {
			Value fresp = cm.request(Proxy::POST, "traderFees", Object("currencyPair", pair));
			FeeInfo &fi = feeMap[StrViewA(pair)];
			fi.fee = fresp["maker"].getNumber()*0.01;
			fi.expiration = now + std::chrono::hours(1);
			return fi.fee;
		} else {
			return iter->second.fee;
		}
	} else {
		return 0.0012;
	}
}

Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	auto iter = std::find_if(all_pairs.begin(), all_pairs.end(), [&](Value v) {
		return v["name"].getString() == pair;
	});
	if (iter == all_pairs.end()) throw std::runtime_error("No such pair. Use 'getAllPairs' to enlist all available pairs");
	Value res(*iter);
	return MarketInfo {
			res["firstCurrency"].getString(),
			res["secondCurrency"].getString(),
			pow(10,-res["lotDecimals"].getNumber()),
			pow(10,-res["priceDecimals"].getNumber()),
			res["minAmount"].getNumber(),
			0,
			getFees(pair)
	};
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		cm.hasKey(),
		"coinmate",
		"Coinmate",
		"https://coinmate.io?referral=U1RjMGVTMUljV2h3VmxkWk0xbHViWFF6WHkxcFFRPT0",
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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAD1BMVEUAAAAsYnlHi6FfqcNXwdo8"
"rDwaAAAAAXRSTlMAQObYZgAAAqpJREFUeAHtmle2IyEQQ5+uvP8tT+ozNQF3OYBeRP9Gt0LjOsDL"
"1gfW1tbW1hZCEm9gDNi2Lz9k2wZeiUOCw3iQAYXtr7uXbEcR1LiXcgzicqcSCBLuXbMI1+z9t0YC"
"ov42SKr0AHYwCfK/5tIdLWovJMAn7i2D0WqC0b5HYGERfO8uA07kgMa+2zG8jEB6tGKHzMtbSPQ5"
"yEv4axH0jUCQQAC6SRAN0+4Jsp8Cv9Zv/kTSbeB2ecnhFIg+PpElEL4xfohYESRs/zOhtG2AwhOy"
"jdQQWDH7HqGKQNC+ZJRPQTuh2zrdj4j4D1O5yaZA/tuLX26A3RDURLnU33CSGJNLgZq/WHE6ALBq"
"OxTt3xs+6UStakPc51I++Rh//25RAaw+RWggX9IEumMZ2VWe5d+B7llFXMVjQRPUInr+t16RAM38"
"eD4BJkrfSEwtMd+F8tRgM78VMbcCiwCsDwIQ+A79xgB6cwA2wIcECHyGvB2A8zth8jv0PMBcDQp/"
"ugmsiQJa6ZHwFsCKmdCK9mAj+empVJ4HuG+6l1aVr1mmWQdb09VrhNurGOGLTaQCFUlDUKeGylSg"
"CAphsD9khSpQNsPtqQT2cDoQSEARFMQhd+dkono3c0rWnxQGDgrx7Yv7+FHpqX97rrNO4Ltfb7jY"
"Yof17b06Dl4Y4HKv4GP3Bc3yrtg7f+Uvrbq9W9FrO93cuXmbe0OWNIBaANum958sAD29ijBzcyue"
"zZ+W+R8EM/5aMIbqTf37VVr7yQ+geRKU828I9LC9WTWId6s116po8Us6uD/6xBTaV0Iw0K6Q5OE1"
"I/oHQxrfNJrkg1KbX5L4Jbu/xA4/Ku2H4whBL6O3etZcs3FG3GRwyL4kcGNP0r0EtseuT7oPkgB8"
"qPaF15YOTVtvbW1tbW1tbX0HniAh/KR9ZWcAAAAASUVORK5CYII="
	};
}


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required one argument" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();
}

