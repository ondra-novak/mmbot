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
#include <cmath>
#include <ctime>

#include "../brokers/api.h"
#include "../imtjson/src/imtjson/stringValue.h"
#include "../shared/linear_map.h"
#include "orderdatadb.h"

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy px;

	Interface(const std::string &path):AbstractBrokerAPI(
			path,
			{
					Object("name","key")("type","string")("label","Key"),
					Object("name","secret")("type","string")("label","Secret")
			}),orderdb(path+".db") {}


	virtual double getBalance(const std::string_view & symb) override;
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
	virtual void enable_debug(bool enable) override {px.debug = enable;}
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;

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

 Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view & pair) {

	 	if (!lastId.hasValue()) {

	 		return TradesSync{ {}, Value(json::array,{time(nullptr), nullptr})};

	 	} else {
	 		time_t startTime = lastId[0].getUIntLong();
	 		Value id = lastId[1];

	 		Value trs = px.private_request("returnTradeHistory", Object
	 				("start", startTime)
	 				("currencyPair",pair)
	 				("limit", 10000));

 			TradeHistory loaded;
	 		for (Value t: trs) {
				auto time = parseTime(String(t["date"]));
				auto id = t["tradeID"];
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

 			std::sort(loaded.begin(),loaded.end(), tradeOrder);
	 		auto iter = std::find_if(loaded.begin(), loaded.end(), [&](auto &&x) {
	 				return x.id == id;
	 		});

	 		if (iter != loaded.end()) {
	 			++iter;
	 			loaded.erase(loaded.begin(),iter);
	 		}

	 		if (!loaded.empty()) {
	 			lastId = {loaded.back().time/1000, loaded.back().id};
	 		}

	 		return TradesSync{ loaded,  lastId};

	 	}
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

static std::uint64_t now() {
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

	double minvol;
	if (cur == "BTC") minvol = 0.0001;
	else if (cur == "ETH") minvol = 0.01;
	else minvol = 1;


	return MarketInfo {
		asst,
		cur,
		0.00000001,
		0.00000001,
		0.00000001,
		minvol,
		getFees(pair),
		income
	};
}

inline double Interface::getFees(const std::string_view &pair) {
	if (px.hasKey()) {
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
				startTime = std::min<std::size_t>(startTime, v.time-1);
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

inline void Interface::onLoadApiKey(json::Value keyData) {
	px.privKey = keyData["secret"].getString();
	px.pubKey = keyData["key"].getString();
}

inline void Interface::onInit() {
	//empty
}

bool Interface::tradeOrder(const Trade &a, const Trade &b) {
	std::size_t ta = a.time;
	std::size_t tb = b.time;
	if (ta < tb) return true;
	if (ta > tb) return false;
	return Value::compare(a.id,b.id) < 0;
}


Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"poloniex",
		"Poloniex",
		"https://www.poloniex.com/",
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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAABwlBMVEUAAAAqNTsqNjwrNjwsNz0t"
"Nz0uOD4vOT8wOkAxO0ExPEEyPEIzPUM0PkQ1PkQ0P0Q1P0U2P0U2QEY3QEY3QUc4QUc4Qkg5QkgA"
"UlwAU1w6Q0kBU10BVF0BVF47REoCVV47RUsCVV88RUsCVl47RksCVl8DVl88Rkw9RkwDV18DV2A9"
"R00+R00EWGAEWWFASU8FWmFASlAFW2JBSlAGW2IGW2NBS1EGXGNCS1FCTFIHXWMHXWRDTFIHXmRD"
"TVNDTlMIX2QIX2VETlQIYGUJYGUJYGZFT1VGT1UJYWYJYWdGUFZHUFYJYmcJYmgJY2gJY2lIUVcJ"
"ZGkJZGpIUlhJUlgJZWoJZWtKU1kJZmsJZmxKVFpLVFoJZ20KZ21MVVpMVVsKaG4KaG8KaW8KaXAK"
"anAKanEKa3EKa3IJbHMKbHIKbHMKbXMKbXQLbXQMbnUMb3YNb3YOb3YPcHcQcHcRcXgScngTcnkU"
"c3kUc3oVc3oWdHsXdHsXdXwYdXwZdnwZdn0adn0bdn4cd34deH8eeH8feYAgeYAheoEieoEie4Ij"
"e4Ije4MkfIMlfIMlfYQmfYQnfYQofYUofoUpfoUpf4Yqf4Zvf8klAAAAAXRSTlMAQObYZgAAApVJ"
"REFUeAHt0YOaHUEUReEd27bHsW3btm3bdvK8sfpO37Oqu6qi6f8F9vrOUaFQKBR+9R7I0U4Qu2AH"
"7RsB72xyssW2VYa3tjdi2ghkemN7LbR2nU22V0BkFZFfwUuBpUDope2FTAuX2MSePbfJsoDIpcD2"
"VIY5tply8sT2WGXNAHLzCMC+QY4eAv8DkAcg9r7uA/8HkHtAKSZPsymTuyBlHyibO4T3KQDcvmXz"
"PwC5CWLv68Z12zUlhA/QVZAlQLlcAe4FyucycS5QTpdsF5UwqSzldgG4FSi/8+eAEiakk4ezwCVA"
"XgFnABfIz2mCBfJ0CpyEAHk7AWQWyN9xcNQsUADHiH41NkFBHAFKgH3vAC4Y/ZP+cIFCOQT7h9MD"
"FM5hcDC1QAEdJCkFCip7gMI6QOoVKLD9RAkjFRwGxLaPqCjYq8j2EsW2h/z/BbuJYttF4geQBn+C"
"7YprO1Fc2+LvQwBQZJtJsf9/B2wiimsDUWTZ9uvqFNh6W2lAbeiCNaRk/zOFlGO/9s8doOobBbMa"
"JPdVFbpgNSndDx2wEpTuhy5YkT+gKsg+MfbjB8B+iILlQAlDq0pUytPiZbbFSqisT36WAdyv/J37"
"g1PJw2L/AK+CRQT2owfwvl/BfMD7fgXz5oLYAbMB7/sVTJ9l432/gqnTgRIGguwFU4FK9AfKaOIU"
"mxS3YCIQB3gVjBlnG68UQQNs46S4BaOAyugN5GhEzn31sHV3DgAqq5utq5wMBzJ0sXUWU00N7Fs6"
"2Tp2EKs21dTI1L6dTagCCLQFAkOG2YTa2FrLNGiATaxNK5tMA4CYWgIZ+vazyUkLYOwDuWkOVFYf"
"IEfNmjazNFUZvWx95KwJUKqetl7KoHEjk/4ihUKhUPgI3TlJWgiZwUMAAAAASUVORK5CYII="

	};
}



int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Argument needed" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();

}
