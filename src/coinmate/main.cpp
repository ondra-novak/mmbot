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
#include "config.h"
#include "proxy.h"
#include "../main/istockapi.h"
#include "../shared/linear_map.h"
#include <brokers/api.h>


using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy &cm;

	Interface(Proxy &cm):cm(cm) {}


	virtual double getBalance(const std::string_view & symb);
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair);
	virtual Orders getOpenOrders(const std::string_view & par);
	virtual Ticker getTicker(const std::string_view & piar);
	virtual json::Value placeOrder(const std::string_view & pair, const Order &order);
	virtual bool reset() ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) ;
	virtual double getFees(const std::string_view &pair) ;
	virtual std::vector<std::string> getAllPairs() ;


	Value balanceCache;
	Value orderCache;
	Array trades;
	bool fetchTrades = true;
	Value firstId;
	Value all_pairs;
	std::size_t lastFrom = -1;

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

 Value Interface::readTradesPerPartes(Value lastId, Value fromTime) {
	 Array result;
	 bool rep;
	 do {
			json::Value args (json::object, {
					json::Value("lastId",lastId),
					json::Value("sort", "ASC"),
					json::Value("timestampFrom",fromTime),
					json::Value("limit",1000)
			});

			Value res = cm.request(Proxy::POST, "tradeHistory", args);
			result.addSet(res);
			rep = !res.empty();
			if (rep) lastId = result[result.size()-1]["transactionId"];

	 } while (rep);
	 return result;
 }

Interface::TradeHistory Interface::getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {

		if (!lastId.defined() && fromTime < lastFrom) {
			trades.clear();
			fetchTrades = true;
		}


		if (fetchTrades) {
			auto fetchLastId = lastId;
			auto fetchFromTime = fromTime;
			if (!trades.empty()) {
				fetchLastId = trades[trades.size()-1]["transactionId"];
			} else {
				firstId = lastId;
			}

//			std::cerr << "[debug] Fetch trades from: " << fetchLastId << std::endl;

			Value res = readTradesPerPartes(fetchLastId, fetchFromTime);
			trades.addSet(res);
			fetchTrades = false;
		}

		auto start = lastId.defined()?
				(lastId == firstId?trades.begin():
						std::find_if(trades.begin(), trades.end(), [&](const Value &v) {
			return v["transactionId"] == lastId;
		})):trades.begin();

		Value result;
		if (start == trades.end() && lastId.defined()) {
			trades.clear();
			Value res = readTradesPerPartes(lastId,fromTime);
			trades.addSet(res);
			start = trades.begin();
			firstId = lastId;
		}

		if (start == trades.end()) return {};

		lastFrom = fromTime?fromTime:trades[0]["createdTimestamp"].getUInt();

		if ((*start)["transactionId"] == lastId)
			++start;
		Array part;
		while (start != trades.end()) {
			Value v = *start;
			if (v["currencyPair"] == pair)
				part.push_back(v);
			++start;
		}
		result = part;

		return mapJSON<TradeHistory> (result, [&](Value x){
			double mlt = x["type"].getString() == "SELL"?-1:1;
			double price = x["price"].getNumber();
			double fee = x["fee"].getNumber();
			double size = x["amount"].getNumber()*mlt;
			double eff_price = price + fee/size;
			return Trade {
				x["transactionId"],
				x["createdTimestamp"].getUInt(),
				size,
				price,
				size,
				eff_price
			};
		});
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
		 r["timestamp"].getUInt()
	 };
}

static const char *place[] = {"sellLimit","buyLimit"};
//static const char *replace[] = {"replaceBySellLimit","replaceByBuyLimit"};


json::Value Interface::placeOrder(const std::string_view & pair, const Order &order) {

	if (order.id.defined()) {
		Value res = cm.request(Proxy::POST, "cancelOrderWithInfo",Object("orderId", order.id));
		if (res["success"].getBool() != true && res["remainingAmount"].getNumber()>0) {
			throw std::runtime_error("Order was not placed, because cancelOrder failed");
		}
	}


	const char *cmd = nullptr;

	double amount;
	const char **cmdset = place;
	if (order.size<0) {
		cmd = cmdset[0];
		amount = -order.size;
	} else {
		cmd = cmdset[1];
		amount = +order.size;
	}

	json::Value args(json::object,{
		json::Value("amount", amount),
		json::Value("price", order.price),
		json::Value("currencyPair", pair),
		json::Value("clientOrderId",order.client_id)
	});

	auto resp = cm.request(Proxy::POST, cmd, args);
	return resp;
}

bool Interface::reset() {
	balanceCache = Value();
	orderCache = Value();
	fetchTrades = true;
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
	if (cm.hasKey) {
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
int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "No config given, terminated" << std::endl;
		return 1;
	}

	try {

		ondra_shared::IniConfig ini;


		if (!ini.load(argv[1],[](const auto &itm){
			throw std::runtime_error(std::string("Unable to process: ")+itm.key.data + " " + itm.value.data);
		})) throw std::runtime_error(std::string("Unable to open: ")+argv[1]);

		Config cfg = load(ini["api"]);
		Proxy coinmate(cfg);
		Interface ifc(coinmate);

		ifc.dispatch();

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

