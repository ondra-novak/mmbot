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
#include <cmath>
#include <ctime>

#include "../imtjson/src/imtjson/stringValue.h"
#include "../shared/linear_map.h"
#include "orderdatadb.h"

using namespace json;

class Interface {
public:
	Proxy &cm;

	Interface(Proxy &cm, std::string dbpath):cm(cm),orderdb(dbpath) {}

	using Call = Value (Interface::*)(Value args);

	using MethodMap = std::unordered_map<std::string_view, Call> ;


	Value getBalance(Value req);
	Value getTrades(Value req);
	Value getOpenOrders(Value req);
	Value getTicker(Value req);
	Value placeOrder(Value req);
	Value reset(Value req);
	Value getBalanceSymbols(Value req);
	Value getAllPairs(Value req);
	Value getInfo(Value req);
	Value getFees(Value req);
	Value call(Value req);


	Value callMethod(std::string_view name, Value args);

	static MethodMap methodMap;

	Value balanceCache;
	Value tickerCache;
	Value orderCache;
	Value feeInfo;
	std::chrono::system_clock::time_point feeInfoExpiration;
	using TradeMap = ondra_shared::linear_map<std::string, std::vector<Value> > ;


	TradeMap tradeMap;
	bool needSyncTrades = true;
	std::size_t lastFromTime = -1;

	void syncTrades(std::size_t fromTime);
	bool syncTradesCycle(std::size_t fromTime);
	bool syncTradeCheckTime(const std::vector<Value> &cont, std::size_t time, Value tradeID);

	static bool tradeOrder(const Value &a, const Value &b);


	OrderDataDB orderdb;
};




Interface::MethodMap Interface::methodMap = {
			{"getBalance",&Interface::getBalance},
			{"getTrades",&Interface::getTrades},
			{"getOpenOrders",&Interface::getOpenOrders},
			{"getTicker",&Interface::getTicker},
			{"placeOrder",&Interface::placeOrder},
			{"reset",&Interface::reset},
			{"getBalanceSymbols",&Interface::getBalanceSymbols},
			{"getAllPairs",&Interface::getAllPairs},
			{"getInfo",&Interface::getInfo},
			{"getFees",&Interface::getFees},
			{"call",&Interface::call}
	};

 Value Interface::getBalance(Value req) {
	 if (!balanceCache.defined()) {
		 balanceCache = cm.private_request("returnCompleteBalances",json::Value());
	 }
	 Value v =balanceCache[req.getString()];
	 if (v.defined()) return v["available"].getNumber()+v["onOrders"].getNumber();
	 else throw std::runtime_error("No such symbol");
}

 Value Interface::getBalanceSymbols(Value) {
	 if (!balanceCache.defined()) {
		 balanceCache = cm.private_request("returnCompleteBalances",json::Value());
	 }
	 Array res;
	 for (Value v: balanceCache) res.push_back(v.getKey());
	 return res;
}

 static std::size_t parseTime(json::String date) {
	 int y,M,d,h,m;
	 float s;
	 sscanf(date.c_str(), "%d-%d-%d %d:%d:%f", &y, &M, &d, &h, &m, &s);
	 float sec;
	 float msec = std::modf(s,&sec)*1000;
	 std::tm t={0};
	 t.tm_year = y - 1900;
	 t.tm_mon = M-1;
	 t.tm_mday = d;
	 t.tm_hour = h;
	 t.tm_min = m;
	 t.tm_sec = static_cast<int>(sec);
	 std::size_t res = timegm(&t) * 1000 + static_cast<std::size_t>(msec);
	 return res;

 }

 Value Interface::getTrades(Value req) {
		auto lastId = req["lastId"];
		auto fromTime = req["fromTime"].getUInt();
		auto pair = req["pair"];

		if (fromTime < lastFromTime) {
			tradeMap.clear();
			needSyncTrades = true;
			lastFromTime = fromTime;
		}

		if (needSyncTrades) {
			syncTrades(fromTime);
			needSyncTrades = false;
		}

		auto trs = tradeMap[pair.getString()];

		auto iter = trs.begin();
		auto end = trs.end();
		if (lastId.defined()) {
			iter = std::find_if(iter, end, [&](const Value &x){
				return x["id"] == lastId;
			});
			if (iter != end) ++iter;
		}

		Array res;
		res.reserve(std::distance(iter,end));
		while (iter != end) {res.push_back(*iter);++iter;}
		return res;
}


 Value Interface::getOpenOrders(Value req) {

	 if (!orderCache.defined()) {
		 orderCache = cm.private_request("returnOpenOrders", Object
				 ("currencyPair","all")
		 );

		 orderdb.commit();
		 for (Value p: orderCache) {
			 for (Value o: p) {
				 Value onum = o["orderNumber"];
				 Value data = orderdb.getOrderData(onum);
				 orderdb.storeOrderData(onum, data);
			 }
		 }
	 }


	 Value ords = orderCache[req.getString()];
	 return ords.map([&](Value order) {
		 return Object
				 ("id", order["orderNumber"])
				 ("time", parseTime(order["date"].toString()))
				 ("clientOrderId", orderdb.getOrderData(order["orderNumber"]))
				 ("size", (order["type"].getString() == "sell"?-1.0:1.0)
						 	 * order["amount"].getNumber())
				 ("price",order["rate"].getNumber());
	 });

}

 Value Interface::getTicker(Value req) {
	 if (!tickerCache.defined()) {
		 tickerCache = cm.getTicker();
	 }
	 Value v =tickerCache[req.getString()];
	 if (v.defined()) return Object
			 ("bid",v["highestBid"])
			 ("ask",v["lowestAsk"])
			 ("last",v["last"])
			 ("timestamp",std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::system_clock::now().time_since_epoch()
					 ).count());
	 else throw std::runtime_error("No such pair");
}

 Value Interface::getAllPairs(Value) {
 	 if (!tickerCache.defined()) {
 		 tickerCache = cm.getTicker();
 	 }
	 Array res;
	 for (Value v: tickerCache) res.push_back(v.getKey());
	 return res;
 }



 Value Interface::placeOrder(Value req) {

	std::string pair = req["pair"].getString();
	auto size = req["size"].getNumber();
	auto price = req["price"].getNumber();
	json::Value clientOrderId = req["clientOrderId"];
	json::Value replaceId = req["replaceOrderId"];


	if (replaceId.defined()) {
		Value z = cm.private_request("cancelOrder", Object
				("currencyPair", pair)
				("orderNumber",  replaceId)
		);
		StrViewA msg = z["message"].getString();
		if (z["success"].getUInt() != 1) throw std::runtime_error(
				std::string("Place order failed on cancel (replace): ").append(msg.data, msg.length));
	}

	StrViewA fn;
	if (size < 0) {
		fn = "sell";
		size = -size;
	} else {
		fn = "buy";
	}

	json::Value res = cm.private_request(fn, Object
			("currencyPair", pair)
			("rate", price)
			("amount", size)
			("postOnly", 1)
	);

	Value onum = res["orderNumber"];
	if (!onum.defined()) throw std::runtime_error("Order was not placed (missing orderNUmber)");
	if (clientOrderId.defined())
		orderdb.storeOrderData(onum, clientOrderId);

	return res["orderNumber"];

}

Value Interface::reset(Value req) {
	balanceCache = Value();
	tickerCache = Value();
	orderCache = Value();
	needSyncTrades = true;
	return Value();
}

inline Value Interface::getInfo(Value req) {

	StrViewA pair = req.getString();
	auto splt = pair.split("_",2);
	StrViewA cur = splt();
	StrViewA asst = splt();

	auto currencies = cm.public_request("returnCurrencies", Value());
	if (!currencies[cur].defined() ||
			!currencies[asst].defined())
				throw std::runtime_error("Unknown trading pair symbol");


	return Object
			("asset_step", 0.00000001)
			("currency_step", 0.00000001)
			("asset_symbol", asst)
			("currency_symbol", cur)
			("min_size", 0.00000001)
			("min_volume",0.0001)
			("fees", getFees(req))
			("feeScheme","income");

}

inline Value Interface::getFees(Value ) {
	if (cm.hasKey) {
		auto now = std::chrono::system_clock::now();
		if (!feeInfo.defined() || feeInfoExpiration < now) {
			feeInfo = cm.private_request("returnFeeInfo", Value());
			feeInfoExpiration = now + std::chrono::hours(1);
		}
		return PreciseNumberValue<double>::create(feeInfo["makerFee"].getString());
	} else {
		return 0.0015;
	}

}

Value Interface::callMethod(std::string_view name, Value args) {
	try {
		auto iter = methodMap.find(name);
		if (iter == methodMap.end()) throw std::runtime_error("Method not implemented");
		return {true, (this->*(iter->second))(args)};
	} catch (Value &e) {
		return {false, e};
	} catch (std::exception &e) {
		return {false, e.what()};
	}
}

Value Interface::call(Value args) {
	Value fn = args[0];
	Value a = args[1];
	if (fn.getString().empty()) throw std::runtime_error("Required [\"funcion\",<args>]");

	return cm.private_request(fn.getString(),a);

}



void Interface::syncTrades(std::size_t fromTime) {
	std::size_t startTime ;
	do {
		startTime = 0;
		startTime--;
		for (auto &&k : tradeMap) {
			if (!k.second.empty()) {
				Value v = k.second.back();
				startTime = std::min(startTime, v["time"].getUInt()-1);
			}
		}
		++startTime;
	} while (syncTradesCycle(std::max(startTime,fromTime)));
}


bool Interface::syncTradesCycle(std::size_t fromTime) {

	Value trs = cm.private_request("returnTradeHistory", Object
			("start", fromTime/1000)
			("currencyPair","all")
			("limit", 10000));

	bool succ = false;
	for (Value p: trs) {
		std::string pair = p.getKey();
		auto && lst = tradeMap[pair];
		std::vector<Value> loaded;
		for (Value t: p) {
			auto time = parseTime(String(t["date"]));
			auto id = t["tradeID"];
			if (syncTradeCheckTime(lst, time, id)) {
				auto size = t["amount"].getNumber();
				auto price = t["rate"].getNumber();
				auto fee = t["fee"].getNumber();
				if (t["type"].getString() == "sell") size = -size;
				double eff_size = size >= 0? size*(1-fee):size;
				double eff_price = size < 0? price*(1-fee):price;
				loaded.push_back(Object
					("time",time)
					("id",id)
					("price", price)
					("size", size)
					("eff_price", eff_price)
					("eff_size", eff_size)
				);
			}
		}
		std::sort(loaded.begin(),loaded.end(), tradeOrder);
		lst.insert(lst.end(), loaded.begin(), loaded.end());
		succ = succ || !loaded.empty();
	}

	return succ;
}




inline bool Interface::syncTradeCheckTime(const std::vector<Value> &cont,
		std::size_t time, Value tradeID) {

	if (cont.empty()) return true;
	const Value &b = cont.back();
	if (b["time"].getUInt() < time) return true;
	if (b["time"].getUInt() == time && Value::compare(b["id"], tradeID) < 0) return true;
	return false;
}


bool Interface::tradeOrder(const Value &a, const Value &b) {
	std::size_t ta = a["time"].getUInt();
	std::size_t tb = b["time"].getUInt();
	if (ta < tb) return true;
	if (ta > tb) return false;
	return Value::compare(a["id"],b["id"]) < 0;
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

		std::string dbpath = ini["order_db"].mandatory["path"].getPath();

		Interface ifc(coinmate, dbpath);



		while (true) {
			int i = std::cin.get();
			if (i == EOF) break;
			std::cin.putback(i);
			Value v = Value::fromStream(std::cin);
			ifc.callMethod(v[0].getString(), v[1]).toStream(std::cout);
			std::cout << std::endl;
		}

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}
