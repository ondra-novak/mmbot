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
#include "../shared/iterator_stream.h"
#include "../brokers/orderdatadb.h"

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

	using Symbols = ondra_shared::linear_map<std::string, MarketInfo, std::less<std::string_view> > ;
	using Tickers = ondra_shared::linear_map<std::string, Ticker,  std::less<std::string_view> >;

	Value balanceCache;
	Tickers tickerCache;
	Value orderCache;
	Value feeInfo;
	std::chrono::system_clock::time_point feeInfoExpiration;
	Symbols symbols;
	using TradeMap = ondra_shared::linear_map<std::string, std::vector<Trade> > ;


	TradeMap tradeMap;
	bool needSyncTrades = true;
	std::size_t lastFromTime = -1;

	void syncTrades(std::size_t fromTime);
	bool syncTradesCycle(std::size_t fromTime);
	bool syncTradeCheckTime(const std::vector<Trade> &cont, std::size_t time, Value tradeID);

	static bool tradeOrder(const Trade &a, const Trade &b);

	void init();

	std::intptr_t time_diff;


};




 double Interface::getBalance(const std::string_view & symb) {
	 if (!balanceCache.defined()) {
		 balanceCache = px.private_request("/api/v3/account",json::object);
		 feeInfo = balanceCache["makerCommission"].getNumber()/10000.0;
	 }
	 Value v =balanceCache["balances"][symb];
	 if (v.defined()) return v["free"].getNumber()+v["locked"].getNumber();
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
	return {};
}

static std::uintptr_t now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();
}

static Value indexBySymbol(Value data) {
	Object bld;
	for (Value d:data) bld.set(d["symbol"].getString(), d);
	return bld;
}

Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	 if (tickerCache.empty()) {
		 Value book = indexBySymbol(px.public_request("/api/v3/ticker/bookTicker", Value()));
		 Value price = indexBySymbol(px.public_request("/api/v3/ticker/price", Value()));
		 auto bs = ondra_shared::iterator_stream(book);
		 auto ps = ondra_shared::iterator_stream(price);
		 std::vector<Tickers::value_type> tk;
		 while (!!bs && !!ps) {
			 Value b = *bs;
			 Value p = *ps;
			 if (b.getKey() < p.getKey()) bs();
			 else if (b.getKey() > p.getKey()) ps();
			 else {
				 tk.push_back(Tickers::value_type(
						 p.getKey(),
						 {
								 b["bidPrice"].getNumber(),
								 b["askPrice"].getNumber(),
								 p["price"].getNumber(),
								 now(),
						 }));
				 bs();
				 ps();
			 }
		 }
		 tickerCache = Tickers(std::move(tk));
	 }

	 auto iter=tickerCache.find(pair);
	 if (iter != tickerCache.end()) return iter->second;
	 else throw std::runtime_error("No such pair");
}

std::vector<std::string> Interface::getAllPairs() {
 	 std::vector<std::string> res;
	 for (auto &&v: symbols) res.push_back(v.first);
	 return res;
 }



json::Value Interface::placeOrder(const std::string_view & pair,
		double size,
		double price,
		json::Value clientId,
		json::Value replaceId,
		double replaceSize) {
return nullptr;
}

bool Interface::reset() {
	balanceCache = Value();
	tickerCache.clear();
	orderCache = Value();
	needSyncTrades = true;
	return true;
}

void Interface::init() {
	Value res = px.public_request("/api/v1/exchangeInfo",Value());

	std::uintptr_t srvtm = res["serverTime"].getUInt();
	std::uintptr_t localtm = now();
	time_diff = srvtm - localtm;

	using VT = Symbols::value_type;
	std::vector<VT> bld;
	for (Value smb: res["symbols"]) {
		MarketInfo nfo;
		nfo.asset_symbol = smb["baseAsset"].getString();
		nfo.currency_symbol = smb["quoteAsset"].getString();
		nfo.currency_step = std::pow(10,-smb["quotePrecision"].getNumber());
		nfo.asset_step = std::pow(10,-smb["baseAssetPrecision"].getNumber());
		nfo.feeScheme = income;
		nfo.min_size = 0;
		nfo.min_volume = 0;
		nfo.fees = 0;
		for (Value f: smb["filters"]) {
			auto ft = f["filterType"].getString();
			if (ft == "LOT_SIZE") {
				nfo.min_size = f["minQty"].getNumber();
				nfo.asset_step = f["stepSize"].getNumber();
			} else if (ft == "PRICE_FILTER") {
				nfo.currency_step = f["tickSize"].getNumber();
			} else if (ft == "MIN_NOTIONAL") {
				nfo.min_volume = f["minNotional"].getNumber();
			}
		}
		std::string symbol = smb["symbol"].getString();
		bld.push_back(VT(symbol, nfo));
	}
	symbols = Symbols(std::move(bld));

}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {

	auto iter = symbols.find(pair);
	if (iter == symbols.end())
		throw std::runtime_error("Unknown trading pair symbol");
	MarketInfo res = iter->second;
	return res;
}

inline double Interface::getFees(const std::string_view &pair) {
	if (px.hasKey) {
		 if (!feeInfo.defined()) {
			 balanceCache = px.private_request("/api/v3/account",json::object);
			 feeInfo = balanceCache["makerCommission"].getNumber()/10000.0;
		 }
		 return feeInfo.getUInt();
	} else {
		return 0.001;
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


		Interface ifc(proxy);

		ifc.init();

		ifc.dispatch();


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}
