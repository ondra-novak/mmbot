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

#include "../brokers/api.h"
#include "../imtjson/src/imtjson/stringValue.h"
#include "../shared/linear_map.h"
#include "../brokers/orderdatadb.h"

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy &px;

	Interface(Proxy &cm, std::string dbpath):px(cm),orderdb(dbpath) {}


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


	Value balanceCache;
	Value tickerCache;
	Value orderCache;
	Value feeInfo;
	std::chrono::system_clock::time_point feeInfoExpiration;
	using TradeMap = ondra_shared::linear_map<std::string, std::vector<Trade> > ;


	TradeMap tradeMap;
	bool needSyncTrades = true;
	std::size_t lastFromTime = -1;

	void syncTrades(std::size_t fromTime);
	bool syncTradesCycle(std::size_t fromTime);
	bool syncTradeCheckTime(const std::vector<Trade> &cont, std::size_t time, Value tradeID);

	static bool tradeOrder(const Trade &a, const Trade &b);


	OrderDataDB orderdb;
};




 double Interface::getBalance(const std::string_view & symb) {
	 if (!balanceCache.defined()) {
		 balanceCache = px.private_request("returnCompleteBalances",json::Value());
	 }
	 Value v =balanceCache[symb];
	 if (v.defined()) return v["available"].getNumber()+v["onOrders"].getNumber();
	 else throw std::runtime_error("No such symbol");
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

 Interface::TradeHistory Interface::getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {

		if (fromTime < lastFromTime) {
			tradeMap.clear();
			needSyncTrades = true;
			lastFromTime = fromTime;
		}

		if (needSyncTrades) {
			syncTrades(fromTime);
			needSyncTrades = false;
		}

		auto trs = tradeMap[pair];

		auto iter = trs.begin();
		auto end = trs.end();
		if (lastId.defined()) {
			iter = std::find_if(iter, end, [&](const Trade &x){
				return x.id == lastId;
			});
			if (iter != end) ++iter;
		}


		return TradeHistory(iter, end);
}


Interface::Orders Interface::getOpenOrders(const std::string_view & pair) {

	 if (!orderCache.defined()) {
		 orderCache = px.private_request("returnOpenOrders", Object
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


	 Value ords = orderCache[pair];
	 return mapJSON(ords, [&](Value order){
		 return Order {
			 order["orderNumber"],
			 orderdb.getOrderData(order["orderNumber"]),
			 (order["type"].getString() == "sell"?-1.0:1.0)
			 						 	 * order["amount"].getNumber(),
			 order["rate"].getNumber()
		 };
	 }, Orders());
}

static std::uintptr_t now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();
}

Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	 if (!tickerCache.defined()) {
		 tickerCache = px.getTicker();
	 }
	 Value v =tickerCache[pair];
	 if (v.defined()) return Ticker {
			 v["highestBid"].getNumber(),
			 v["lowestAsk"].getNumber(),
			 v["last"].getNumber(),
			 now()
	 	 };
	 else throw std::runtime_error("No such pair");
}

std::vector<std::string> Interface::getAllPairs() {
 	 if (!tickerCache.defined()) {
 		 tickerCache = px.getTicker();
 	 }
 	 std::vector<std::string> res;
	 for (Value v: tickerCache) res.push_back(v.getKey());
	 return res;
 }



json::Value Interface::placeOrder(const std::string_view & pair,
		double size,
		double price,
		json::Value clientId,
		json::Value replaceId,
		double replaceSize) {

	//just inicialize order_db
	getOpenOrders(pair);


	if (replaceId.defined()) {
		Value z = px.private_request("cancelOrder", Object
				("currencyPair", pair)
				("orderNumber",  replaceId)
		);
		StrViewA msg = z["message"].getString();
		if (z["success"].getUInt() != 1  ||
				z["amount"].getNumber()<std::fabs(replaceSize)*0.999999)
				throw std::runtime_error(
						std::string("Place order failed on cancel (replace): ").append(msg.data, msg.length));
	}

	StrViewA fn;
	if (size < 0) {
		fn = "sell";
		size = -size;
	} else if (size > 0){
		fn = "buy";
	} else {
		return nullptr;
	}

	json::Value res = px.private_request(fn, Object
			("currencyPair", pair)
			("rate", price)
			("amount", size)
			("postOnly", 1)
	);

	Value onum = res["orderNumber"];
	if (!onum.defined()) throw std::runtime_error("Order was not placed (missing orderNUmber)");
	if (clientId.defined())
		orderdb.storeOrderData(onum, clientId);

	return res["orderNumber"];

}

bool Interface::reset() {
	balanceCache = Value();
	tickerCache = Value();
	orderCache = Value();
	needSyncTrades = true;
	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {

	auto splt = StrViewA(pair).split("_",2);
	StrViewA cur = splt();
	StrViewA asst = splt();

	auto currencies = px.public_request("returnCurrencies", Value());
	if (!currencies[cur].defined() ||
			!currencies[asst].defined())
				throw std::runtime_error("Unknown trading pair symbol");


	return MarketInfo {
		asst,
		cur,
		0.00000001,
		0.00000001,
		0.00000001,
		0.0001,
		getFees(pair),
		income
	};
}

inline double Interface::getFees(const std::string_view &pair) {
	if (px.hasKey) {
		auto now = std::chrono::system_clock::now();
		if (!feeInfo.defined() || feeInfoExpiration < now) {
			feeInfo = px.private_request("returnFeeInfo", Value());
			feeInfoExpiration = now + std::chrono::hours(1);
		}
		return feeInfo["makerFee"].getNumber();
	} else {
		return 0.0015;
	}

}



void Interface::syncTrades(std::size_t fromTime) {
	std::size_t startTime ;
	do {
		startTime = 0;
		startTime--;
		for (auto &&k : tradeMap) {
			if (!k.second.empty()) {
				const auto &v = k.second.back();
				startTime = std::min(startTime, v.time-1);
			}
		}
		++startTime;
	} while (syncTradesCycle(std::max(startTime,fromTime)));
}


bool Interface::syncTradesCycle(std::size_t fromTime) {

	Value trs = px.private_request("returnTradeHistory", Object
			("start", fromTime/1000)
			("currencyPair","all")
			("limit", 10000));

	bool succ = false;
	for (Value p: trs) {
		std::string pair = p.getKey();
		auto && lst = tradeMap[pair];
		std::vector<Trade> loaded;
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
				loaded.push_back(Trade {
					id,
					time,
					size,
					price,
					eff_size,
					eff_price,
				});
			}
		}
		std::sort(loaded.begin(),loaded.end(), tradeOrder);
		lst.insert(lst.end(), loaded.begin(), loaded.end());
		succ = succ || !loaded.empty();
	}

	return succ;
}




inline bool Interface::syncTradeCheckTime(const std::vector<Trade> &cont,
		std::size_t time, Value tradeID) {

	if (cont.empty()) return true;
	const Trade &b = cont.back();
	if (b.time < time) return true;
	if (b.time == time && Value::compare(b.id, tradeID) < 0) return true;
	return false;
}


bool Interface::tradeOrder(const Trade &a, const Trade &b) {
	std::size_t ta = a.time;
	std::size_t tb = b.time;
	if (ta < tb) return true;
	if (ta > tb) return false;
	return Value::compare(a.id,b.id) < 0;
}


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

		std::string dbpath = ini["order_db"].mandatory["path"].getPath();

		Interface ifc(proxy, dbpath);


		ifc.dispatch();


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}
