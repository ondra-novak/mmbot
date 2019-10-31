/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <imtjson/operations.h>
#include "shared/toString.h"
#include "proxy.h"
#include <cmath>
#include <ctime>

#include "../brokers/api.h"
#include <imtjson/stringValue.h>
#include "../shared/linear_map.h"
#include "../shared/iterator_stream.h"
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>
#include "../imtjson/src/imtjson/string.h"
#include "../main/sgn.h"

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy px;

	Interface(const std::string &path):AbstractBrokerAPI(path, {
			Object
				("name","key")
				("label","ID")
				("type","string"),
			Object
				("name","secret")
				("label","Secret")
				("type","string"),
			Object
				("name","server")
				("label","Server")
				("type","enum")
				("options",Object
						("main","www.bitmex.com")
						("testnet","testnet.bitmex.com"))
				("default","main")})
	,optionsFile(path+".conf"){}


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
	virtual void enable_debug(bool enable) override;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;


	struct SymbolInfo {
		String id;
		bool inverse;
		double multiplier;
		double lotSize;
		double leverage;
		double tickSize;

	};

	using SymbolList = ondra_shared::linear_map<std::string_view, SymbolInfo>;

	SymbolList slist;

	const SymbolInfo &getSymbol(const std::string_view &id);
	virtual json::Value getSettings(const std::string_view &) const override;
	virtual void setSettings(json::Value v) override;



private:
	std::size_t uid_cnt = Proxy::now();
	void updateSymbols();

	Value balanceCache;
	Value positionCache;
	Value orderCache;

	Value readOrders();


	std::size_t quoteEachMin = 5;
	bool allowSmallOrders = false;
	std::string optionsFile;

	void saveOptions();
	void loadOptions();
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

inline double Interface::getBalance(const std::string_view &symb) {
	if (symb == "BTC") {
		if (!balanceCache.hasValue()) {
			balanceCache = px.request("GET","/api/v1/user/margin", Object("currency","XBt")
						("columns",{"marginBalance"}));
		}
		return balanceCache["marginBalance"].getNumber()*1e-8;

	} else {
		const SymbolInfo &s = getSymbol(symb);
		if (!positionCache.hasValue()) {
			positionCache = px.request("GET","/api/v1/position",Object("columns",{"symbol","currentQty"}));
		}
		Value x = positionCache.find([&](Value v){return v["symbol"] == symb;});
		double q = x["currentQty"].getNumber();
		if (s.inverse) q = -q;
		return q;
	}
	return 0;
}

static std::size_t parseTime(json::String date) {
	 int y,M,d,h,m;
	 float s;
	 sscanf(date.c_str(), "%d-%d-%dT%d:%d:%fZ", &y, &M, &d, &h, &m, &s);
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




inline Interface::TradesSync Interface::syncTrades(json::Value lastId,  const std::string_view &pair) {
	const SymbolInfo &s = getSymbol(pair);
	Value trades;
	Value lastExecId = lastId[1];
	Value columns = {"execID","transactTime","side","lastQty","lastPx","symbol","execType"};
	if (lastId.hasValue()) {
		trades = px.request("GET","/api/v1/execution/tradeHistory",Object
				("filter", Object("execType",Value(json::array,{"Trade"})))
				("startTime",lastId[0])
				("count", 100)
				("symbol", pair)
				("columns",columns));
	} else {
		trades = px.request("GET","/api/v1/execution/tradeHistory",Object
				("filter", Object("execType",Value(json::array,{"Trade"})))
				("reverse",true)
				("count", 1)
				("symbol", pair)
				("columns",columns));

	}

	auto idx = trades.findIndex([&](Value item) {
		return item["execID"] == lastExecId;
	});
	if (idx != -1) {
		trades = trades.slice(idx+1);
	}

	Value lastExecTime = lastId[0];
	TradesSync resp;
	for (Value item: trades) {
		lastExecId = item["execID"];
		lastExecTime = item["transactTime"];
		StrViewA side = item["side"].getString();
		double mult = side=="Buy"?1:side=="Sell"?-1:0;
		if (mult == 0) continue;
		if (s.inverse) mult=-mult;
		double size = mult*item["lastQty"].getNumber()*s.multiplier;
		double price = s.inverse?1.0/item["lastPx"].getNumber():item["lastPx"].getNumber();
		resp.trades.push_back(Trade{
			lastExecId,
			parseTime(lastExecTime.toString()),
			size,
			price,
			size,
			price
		});
	}

	if (resp.trades.empty()) {
		resp.lastId = lastId;
	} else {
		resp.lastId = {lastExecTime, lastExecId};
	}
	return resp;
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {
	const SymbolInfo &s = getSymbol(pair);

	Value orders = readOrders();
	Value myorders = orders.filter([&](const Value &v) {
		return v["symbol"].getString() == pair;
	});
	Orders resp;
	for (Value ord: myorders) {
		double mult = ord["side"].getString() == "Sell"?-1:1;
		double size = ord["orderQty"].getNumber();
		double price = ord["price"].getNumber();
		StrViewA clid = ord["clOrdID"].getString();
		Value id = ord["orderID"];
		Value clientId;
		if (!clid.empty()) try{
			clientId = Value::fromString(clid)[0];
		} catch (...) {

		}
		if (s.inverse) {
			mult = -mult;
			price = 1/price;
		}
		resp.push_back(Order {
			id,clientId,size*s.multiplier*mult,price
		});

	}
	return resp;
}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	const SymbolInfo &s = getSymbol(pair);
	Value resp = px.request("GET","/api/v1/orderBook/L2", Object("symbol",pair)("depth",1));
	Ticker t;
	for (Value v: resp) {
		double price = v["price"].getNumber();
		if (v["side"].getString() == "Sell") {
			if (s.inverse) t.bid =1/price; else t.ask = price;
		}
		else if (v["side"].getString() == "Buy") {
			if (s.inverse) t.ask =1/price; else t.bid = price;
		}
	}
	return Ticker{t.bid, t.ask, sqrt(t.bid*t.ask), px.now()*1000};
}

static bool almostSame(double a, double b) {
	double mdl = (fabs(a) + fabs(b))/2;
	return fabs(a - b) < mdl*1e-6;
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	std::intptr_t now = std::intptr_t(px.now()*1000);

	const SymbolInfo &s = getSymbol(pair);
	if (s.inverse && price) {
		size = -size;
		price = 1/price;
		price = round(price/s.tickSize)*s.tickSize;
	}

	Value side = size < 0?"Sell":"Buy";
	Value qty = fabs(size/s.multiplier);

	Value curOrders = readOrders();
	if (replaceId.hasValue()) {
		Value toCancel = curOrders.find([&](Value v) {
			return v["orderID"] == replaceId;
		});
		if (toCancel.hasValue()) {
			std::intptr_t  orderTime = std::intptr_t (parseTime(toCancel["transactTime"].getString()));
			std::intptr_t limitTime = std::intptr_t(quoteEachMin*60000);
			if (size != 0 && quoteEachMin && now-orderTime < limitTime) {
				if (px.debug) std::cerr << "Re-quote disallowed for this time (" << (now-orderTime) <<" < " << limitTime <<") "<< std::endl;
				return toCancel["orderID"];
			}
			if (toCancel["Side"] == side && toCancel["symbol"].getString() == pair
					&& almostSame(toCancel["orderQty"].getNumber() , qty.getNumber())
					&& almostSame(toCancel["price"].getNumber() , price)) {
				return toCancel["orderID"];
			} else {
				Value resp = px.request("DELETE","/api/v1/order",Object("orderID",replaceId));
				Value remain = resp[0]["orderQty"].getNumber();
				if (!almostSame(remain.getNumber(), replaceSize) && remain.getNumber() < replaceSize) {
					return nullptr;
				}
			}
		}
	}
	if (size == 0) return nullptr;
	Value clId;
	if (clientId.hasValue()) {
		clId = {clientId, ++uid_cnt};
		clId = clId.toString();
	}
	Object order;
	order.set("symbol", pair)
			 ("side",side)
			 ("orderQty",qty)
			 ("price",price)
			 ("clOrdID", clId)
			 ("ordType","Limit")
			 ("execInsts","ParticipateDoNotInitiate");
	Value resp = px.request("POST","/api/v1/order",Value(),order);
	return resp["orderID"];
}

inline bool Interface::reset() {
	balanceCache = nullptr;
	positionCache = nullptr;
	orderCache = nullptr;


	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	const SymbolInfo &s = getSymbol(pair);

	if (s.inverse) {
		return MarketInfo{
			std::string(pair),
			"BTC",
			s.multiplier*s.lotSize,
			0,
			s.multiplier*s.lotSize,
			allowSmallOrders?0:0.0026,
			0,
			currency,
			s.leverage,
			true,
			"USD",
		};
	} else {
		return MarketInfo{
			std::string(pair),
			"BTC",
			s.multiplier*s.lotSize,
			s.tickSize,
			s.multiplier*s.lotSize,
			allowSmallOrders?0:0.0026,
			0,
			currency,
			s.leverage,
			false,
			"XBT"
		};
	}
}

inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

void Interface::enable_debug(bool enable) {
	px.debug = enable;
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"bitmex",
		"BitMEX",
		"https://www.bitmex.com/",
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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAYAAADDPmHLAAAB1klEQVR42u3a0VECQRBF0dEyEb4x"
"K+IyK/kmFE2Bhemd7ulzEtBa3vbFKscAAAAAAAAAAAAAdvCx8oc/vq9/VR7U5fce9qyut8f053D/"
"uTz1+356B9aK+PCPMIANPfv2Lx1ApfO/Mxeg8fk3gARfAA2AZf1fNgD9dwH0P0H/DaB5/w2gef+X"
"DED/XQD9T9J/A2jefwNo3v/TB6D/LoD+J+q/ATTvvwE07/+pA9B/F0D/k/XfAJr33wCa9/+0Aei/"
"C6D/CftvAM37bwDN+3/KAPTfBdD/pP03gOb9N4Dm/Q8fgP67APqfuP9jjPGl1S6AD79p/0MHoP8u"
"gP4n778BUGsA+j+3/2ED0H8XQP8L9N8AqDMA/Z/f/5AB6L8LoP9F+m8A1BiA/sf0f/oA9N8F0P9C"
"/TcA8g9A/+P6P3UA+u8C6H+x/hsAuQeg/7H9H2PSfwVX63/FUy0BGIC3v9EA9D++/1MG4O9/FwAD"
"0H8D0P9y/X97APrvAmAA+m8A+l+y/28NQP9dAAxA/w1A/8v2/+UB6L8LgAHovwHof+n+vzQA/XcB"
"MAD9NwD9L9//wwPQfxcAA9B/A9D/Lfp/aAD67wJgAPpvAPq/Tf8BAAAAAAAAAAAA2MQ/A0e46eQ0"
"zFcAAAAASUVORK5CYII=", true
	};
}

inline std::vector<std::string> Interface::getAllPairs() {
	if (slist.empty()) updateSymbols();
	std::vector<std::string> out;
	out.reserve(slist.size());
	for (auto &&k: slist) {
		out.push_back(k.second.id.str());
	}
	return out;
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	px.setTestnet(keyData["server"].getString() == "testnet");
	px.privKey = keyData["secret"].getString();
	px.pubKey = keyData["key"].getString();
}

inline void Interface::onInit() {
	loadOptions();
}

void Interface::updateSymbols() {
	Value resp = px.request("GET", "/api/v1/instrument/active",
			Object("columns",{"optionUnderlyingPrice","isQuanto","settlCurrency","symbol","isInverse","rootSymbol","quoteCurrency","multiplier","lotSize","initMargin","tickSize"}));
	std::vector<SymbolList::value_type> smap;
	for (Value s : resp) {
		if (s["optionUnderlyingPrice"].hasValue())
			continue;

		if (s["isQuanto"].getBool())
			continue;

		if (s["settlCurrency"].getString() != "XBt")
			continue;

		SymbolInfo sinfo;
		sinfo.id = s["symbol"].toString();
		sinfo.inverse = s["isInverse"].getBool();
		if (sinfo.inverse) {
			if (s["rootSymbol"].getString() != "XBT")
				continue;
		} else {
			if (s["quoteCurrency"].getString() != "XBT")
				continue;
		}
		sinfo.multiplier = fabs(s["multiplier"].getNumber())/ 100000000.0;
		sinfo.lotSize = s["lotSize"].getNumber();
		sinfo.leverage = 1/s["initMargin"].getNumber();
		sinfo.tickSize = s["tickSize"].getNumber();
		smap.push_back( { sinfo.id.str(), sinfo });
	}
	slist.swap(smap);
}

const Interface::SymbolInfo& Interface::getSymbol(const std::string_view &id) {
	if (slist.empty()) {
		updateSymbols();
	}
	auto iter = slist.find(id);
	if (iter == slist.end()) throw std::runtime_error("Unknown symbol");
	return iter->second;
}

inline json::Value Interface::getSettings(const std::string_view&) const {
	char m[4];
	m[0] = 'm';
	m[1] = (quoteEachMin/10)%10+'0';
	m[2] = quoteEachMin%10+'0';
	m[3] = 0;


	return {
		Object
			("name","quoteEachMin")
			("label","Allow to move the order")
			("type","enum")
			("options",Object
					("m00", "anytime")
					("m01", "every 1 minute")
					("m02", "every 2 minutes")
					("m03", "every 3 minutes")
					("m04", "every 4 minutes")
					("m05", "every 5 minutes")
					("m07", "every 7 minutes")
					("m10", "every 10 minutes")
					("m15", "every 15 minutes")
					("m20", "every 20 minutes")
					("m30", "every 30 minutes"))
			("default",m),
		Object
			("name","allowSmallOrders")
			("label","Allow small orders (spam orders)")
			("type","enum")
			("options", Object
					("allow", "Allow (not recommended)")
					("disallow", "Disallow"))
			("default",allowSmallOrders?"allow":"disallow")
	};
}

inline void Interface::setSettings(json::Value v) {
	quoteEachMin = std::strtod(v["quoteEachMin"].getString().data+1,nullptr);
	allowSmallOrders = v["allowSmallOrders"].getString() == "allow";
	saveOptions();
}

Value Interface::readOrders() {
	if (!orderCache.hasValue()) {
		orderCache = px.request("GET","/api/v1/order",Object
				("filter",Object
						("ordStatus",{"New","PartiallyFilled","DoneForDay","Stopped"}))
		);
	}
	return orderCache;


}

inline void Interface::saveOptions() {
	Object opt;
	opt.set("quoteEachMin",quoteEachMin);
	opt.set("allowSmallOrders", allowSmallOrders);
	std::ofstream file(optionsFile,std::ios::out|std::ios::trunc);
	Value(opt).toStream(file);
}

inline void Interface::loadOptions() {
	try {
		std::ifstream file(optionsFile, std::ios::in);
		if (!file) return;
		Value v = Value::fromStream(file);
		quoteEachMin = v["quoteEachMin"].getUInt();
		allowSmallOrders = v["allowSmallOrders"].getUInt();
	} catch (std::exception &e) {
		std::cerr<<"Failed to load config: " << e.what() << std::endl;
	}
}
