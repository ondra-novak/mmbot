/*
 * interface.cpp
 *
 *  Created on: 3. 9. 2020
 *      Author: ondra
 */

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/parser.h>
#include <imtjson/operations.h>
#include <imtjson/binary.h>
#include "interface.h"

#include <iomanip>

#include "../../shared/stringview.h"
using json::Object;
using json::Value;
using json::String;



Interface::MarketType Interface::getMarketType(std::string_view pair) {
	std::string_view pfx = pair.substr(0,3);
	if (pfx == "lv_") return MarketType::leveraged;
	else if (pfx == "hb_") return MarketType::hybrid;
	else return MarketType::exchange;
}



IBrokerControl::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		hasKey(),
		"kraken",
		"Kraken",
		"https://www.kraken.com/",
		"1.0",
		R"mit(Copyright (c) 2019 Ondřej Novák

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software withourestriction, including without limitation the rights to use,
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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAM1BMVEVAAAA2ANE/F9dDItpMItZG"
"KdhCLtZLLNtPM9pKOdxSOddXPNtRQd1YQdhXQ9JaQtlbQ9r89RaIAAAAAXRSTlMAQObYZgAAAjZJ"
"REFUeNrtmtlywyAMRZ04jm0kWfz/1zaTtpM6Aa4XlpmOzmscdEFCLKLrDMMwDMMwDjI4IpVflNw0"
"VLN9GUkfyBr1TNOluPHekaR4iLgVND+rbECpkDdIN9l/DkQB68Kyi7wanJfdqM9m/q5yCD/nsb/I"
"cTKYZ5Uz6Fn7cpZzwciSAW3Y/Z9gbGyf6WAsCudSIEIH7KvkxDXr/tGUIPlpOP7fUNv+75qOUgjX"
"cPz3hIEvZn9bVnZSEG4YAE8W19IBm5ygUhjf0gEbpkIF+5ze/UvbIdAa9rWx/ZQCqYRrkQO3RIE2"
"FkDV7Ef2JvUGIJwOJ6kIlzqGnZmJVFOATO2SQCwVzHUFUO2NCAyCm9YVIPdmaTjig9r2P3JRbQ+I"
"ru9z++oC3nwwhu86mSk5O1SZOfmF96xe8ZI4xfO1Y9QJgjmfGAn4TEMzilB1MIvMySBLz0IcojCP"
"LWCeJVcivFdwcB6jxWZIrcVos8ALXM0ZrTa3hLoRrZWKT7Vwve3/FsJg8wR3NLS3jWtcAMENW6AU"
"0e9tIzECgeGbFG1oLqsvdDojgGHZJlCYW7cREjDqVgGB4RuwADUBJsAEmAATYAJMgAkwASbABPxv"
"AfeyAjp4NMshYEoI4MQNWuj0fUUlL8UlkfgVjccFHVjz8jsFdODo+/ZvhlU/XBaL/8awqqjg5R9H"
"C3OvJiSqLlrXdArLz0kHrBwZauN5I5l84bDtC3bwhUj0C/wyOsfb6WtnGIZhGIZhGC++AKuyWLMB"
"sxKhAAAAAElFTkSuQmCC",
true,true
	};
}

Interface::Interface(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path,
			{
						Object({
							{"name","key"},
							{"label","API Key"},
							{"type","string"}
						}),
						Object({
							{"name","secret"},
							{"label","API secret"},
							{"type","string"}
						})
			})

	,api("https://api.kraken.com"),
	orderDB(secure_storage_path+".db")
{
	json::enableParsePreciseNumbers=true;
	nonce = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

json::Value Interface::getMarkets() const {
	const_cast<Interface *>(this)->updateSymbols();
	Object res;
	for (Value row: pairMap) {
		auto pairName = row.getKey();
		auto base = row["base"].getString();
		auto quote = row["quote"].getString();
		auto altbase = symbolMap[base].getString();
		auto altquote = symbolMap[quote].getString();
		auto exsub = res.object(altbase);
		if (!row["leverage_buy"].empty() && !row["leverage_sell"].empty() && allow_margin)  {
			exsub.set(altquote, Object({
				{"Exchange",String({"ex_",pairName})},
				{"Leveraged",String({"lv_",pairName})},
				{"Hybrid",String({"hb_",pairName})}
			}));
		} else {
			exsub.set(altquote, String({"ex_",pairName}));
		}
	}
	return res;
}

std::vector<std::string> Interface::getAllPairs() {
	updateSymbols();
	std::vector<std::string> res;
	for (Value row: pairMap) {
		res.push_back(String({"ex_",row.getKey()}).str());
		if (!row["leverage_buy"].empty() && !row["leverage_sell"].empty() && allow_margin)  {
			res.push_back(String({"lv_",row.getKey()}).str());
			res.push_back(String({"hb_",row.getKey()}).str());
		}
	}
	return res;
}


void Interface::updateSymbols() {
	auto now = std::chrono::system_clock::now();
	if (mapExpire > now) return;

	Value assets = public_GET("/0/public/Assets");
	assets = assets["result"];
	Object assetsMap;
	Object iassetsMap;
	for (Value row: assets) {
		Value alt = row["altname"];
		assetsMap.set(row.getKey(), alt);
		iassetsMap.set(alt.getString(), row.getKey());
	}
	assetsMap.set("XXBT","BTC");
	iassetsMap.set("BTC","XXBT");
	this->symbolMap = assetsMap;
	this->isymbolMap = iassetsMap;

	this->pairMap = public_GET("/0/public/AssetPairs")["result"];
	this->pairMap = this->pairMap.filter([&](Value row){
		return row["lot"].getString() == "unit"
				&& row["lot_multiplier"].getNumber() == 1 && !ondra_shared::StrViewA(row.getKey()).endsWith(".d");
	});


	mapExpire = now+std::chrono::hours(1);
}

json::Value Interface::checkError(json::Value v) {
	if (v["error"].empty()) return v;
	else throw std::runtime_error(v["error"].join(" ").str());
}

json::Value Interface::public_GET(std::string_view path) {
	try {
		return checkError(api.GET(path));
	} catch (HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

json::Value Interface::public_POST(std::string_view path, json::Value req) {
	try {
		return checkError(api.POST(path, req));
	} catch (HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

json::Value Interface::private_POST(std::string_view path, json::Value req) {
	auto nonce = this->nonce++;

	std::ostringstream buff;
	buff << "nonce="<<nonce;
	for (Value v: req) {
		buff << "&" << v.getKey() << "=" << v.toString();
	}
	std::string sreq = buff.str();

	buff.str("");buff.clear();
	buff << nonce;
	buff << sreq;
	std::string noncepost = buff.str();

	unsigned char sha256_digest[SHA256_DIGEST_LENGTH];
	unsigned char hmac_digest[256];
	SHA256(reinterpret_cast<const unsigned char *>(noncepost.data()), noncepost.size(),sha256_digest);

	buff.str("");buff.clear();
	buff << path;
	buff.write(reinterpret_cast<const char *>(sha256_digest), SHA256_DIGEST_LENGTH);
	std::string toSign = buff.str();

	unsigned int hmac_digest_len = sizeof (hmac_digest);
	HMAC(EVP_sha512(), apiSecret.data(), apiSecret.size(), reinterpret_cast<const unsigned char *>(toSign.data()), toSign.length(), hmac_digest, &hmac_digest_len);
	Value sign = json::base64->encodeBinaryValue(json::BinaryView(hmac_digest, hmac_digest_len));
	Value headers = Object({
		{"API-Key", apiKey},
		{"API-Sign", sign},
		{"Content-Type", "application/x-www-form-urlencoded"}
	});
	try {
		return checkError(api.POST(path, sreq, std::move(headers)));
	} catch (HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

void Interface::processError(HTTPJson::UnknownStatusException &e) {
	json::Value resp = e.body;
	json::Value err = resp["error"];
	if (err.hasValue()) throw std::runtime_error(err.join(" ").str());
	throw e;
}

void Interface::onLoadApiKey(json::Value keyData) {
	apiKey = keyData["key"].getString();
	apiSecret = keyData["secret"].getBinary(json::base64);
}

bool Interface::hasKey() const {
	return !apiKey.empty() && !apiSecret.empty();
}

IStockApi::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	updateSymbols();
	std::string_view psymb = stripPrefix(pair);
	Value symbinfo = pairMap[psymb];
	if (!symbinfo.hasValue())
		throw std::runtime_error("Unknown symbol");

	MarketInfo minfo;
	minfo.asset_symbol = symbolMap[symbinfo["base"].getString()].getString();
	minfo.currency_symbol = symbolMap[symbinfo["quote"].getString()].getString();
	minfo.asset_step = std::pow(10,-symbinfo["lot_decimals"].getInt());
	minfo.currency_step = std::pow(10,-symbinfo["pair_decimals"].getInt());
	minfo.feeScheme = currency;
	minfo.fees=getFees(pair);
	minfo.invert_price = false;
	minfo.leverage = 0;
	minfo.min_size = symbinfo["ordermin"].getNumber();
	minfo.min_volume = 0;
	minfo.private_chart =false;
	minfo.simulator = false;

	auto t = getMarketType(pair);

	if (t == MarketType::leveraged || t==MarketType::hybrid) {
		int lev_buy = symbinfo["leverage_buy"].reduce([](int l, Value v){
			return std::max<int>(l, v.getInt());
		},0);
		int lev_sell = symbinfo["leverage_sell"].reduce([](int l, Value v){
			return std::max<int>(l, v.getInt());
		},0);
		minfo.leverage = std::min(lev_buy, lev_sell);
		minfo.wallet_id = "margin";
	} else {
		minfo.wallet_id = "spot";
	}
	return minfo;
}

AbstractBrokerAPI* Interface::createSubaccount(const std::string &secure_storage_path) {
	return new Interface(secure_storage_path);
}

double Interface::getSpotBalance(const std::string_view &symb) {
	if (!balanceMap.defined()) {
		balanceMap = private_POST("/0/private/Balance",Value());
	}
	return balanceMap["result"][symb].getNumber();
}


double Interface::getCollateral(const std::string_view &symb)  {
	Value bal = private_POST("/0/private/TradeBalance",Object({{"asset",symb}}));
	return bal["result"]["e"].getNumber();
}

double Interface::getPosition(const std::string_view &market) {
	if (!positionMap.defined()) {
		positionMap = private_POST("/0/private/OpenPositions",Value());
	}

	double pos = positionMap["result"].reduce([&](double a, Value z) {
		if (z["pair"].getString() == market) {
			return a + (z["vol"].getNumber()-z["vol_closed"].getNumber())*(z["type"].getString() == "sell"?-1.0:1.0);
		} else {
			return a;
		}
	},0.0);
	return pos;

}

double Interface::getBalance(const std::string_view &symb, const std::string_view &pair) {
	updateSymbols();
	auto tsymb = isymbolMap[symb].getString();
	auto market = stripPrefix(pair);
	switch (getMarketType(pair)) {
	case MarketType::exchange:  return getSpotBalance(tsymb);
	case MarketType::leveraged:
		if (pairMap[market]["base"].getString() == tsymb) return getPosition(market);
		else return getCollateral(tsymb);
	case MarketType::hybrid:
		if (pairMap[market]["base"].getString() == tsymb) return getSpotBalance(tsymb)+getPosition(market);
		else return getCollateral(tsymb);
	}
	return 0;

}

IStockApi::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	bool first_call = false;
	Value startId = lastId[0];
	Value duplist = lastId[1];
	Value apires;
	Value newduplist = json::array;
	auto maxHistory = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()-std::chrono::hours(24)); //24 hour without trade

	if (lastId.type() != json::array || lastId[0].type() != json::string) {
		lastId = json::array;
		first_call = true;
		Value &k = syncTradeCache[nullptr];
		if (k.hasValue()) apires = k;
		else k = apires = private_POST("/0/private/TradesHistory",Object({{"start",maxHistory}}));
		startId = "";
	} else {
		startId = lastId[0];
		duplist = lastId[1];
		Value &k = syncTradeCache[startId];
		if (k.hasValue()) apires = k;
		else k = apires = private_POST("/0/private/TradesHistory",Object({{"start", !startId.getString().empty()?startId:Value(maxHistory)}}));
	}

	updateSymbols();

	auto symb = stripPrefix(pair);
//	auto mt = getMarketType(pair);
	auto altname = pairMap[symb]["altname"].getString();

	Value trades = apires["result"]["trades"].map([&](Value x){
		Value id = x.getKey();
		return x.replace("id", id);
	},json::array).sort([](Value a, Value b){
		return a["time"].getNumber() - b["time"].getNumber();
	});
	if (!trades.empty())  {
		double bestTime=0;
		json::Array dl;
		for (Value x: trades) {
			double tm = x["time"].getNumber();
			if (bestTime < tm) {
				dl.clear();
				bestTime = tm;
				startId = x["id"];
			} else if (bestTime == tm) {
				dl.push_back(x["id"]);
			}
		}
		newduplist = dl;
	}

	lastId = {startId, newduplist};
	if (!first_call) {
		auto out = mapJSON(trades.filter([&](Value x){
			if (duplist.indexOf(x["id"]) != duplist.npos) return false;
			return x["pair"].getString() == symb || x["pair"].getString() == altname;
		}),[&](Value row){
			double price = row["price"].getNumber();
			Value id = row["id"];
			double size = row["vol"].getNumber();
			if (row["type"].getString() == "sell") size = -size;
			double fee = row["fee"].getNumber();
			double eff_price = price + fee/size;
			auto tm = static_cast<std::uint64_t>(row["time"].getNumber()*1000);
			return Trade{id,tm,size,price,size,eff_price};
		}, TradeHistory());
		return {out, lastId};
	} else {
		return {{}, lastId};
	}
}

void Interface::onInit() {}

bool Interface::reset() {
	tickerValid = false;
	balanceMap = Value();
	positionMap = Value();
	orderMap = Value();
	syncTradeCache.clear();
	hDataCache = Value();
	return true;
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {

	updateSymbols();

	auto symb = stripPrefix(pair);
	auto mt = getMarketType(pair);
	if (!orderMap.defined()) {
		orderMap =  private_POST("/0/private/OpenOrders",Value());
	}
	auto altname = pairMap[symb]["altname"].getString();
	Value data = orderMap["result"]["open"];
	data = data.filter([&](Value row) {
		Value descr = row["descr"];
		auto pair = descr["pair"].getString();
		return (symb == pair || altname ==  pair)
				&& (mt == MarketType::hybrid || (mt == MarketType::exchange) == (descr["leverage"].getString() == "none"))
				&& descr["ordertype"].getString() == "limit"
				&& row["status"].getString() !=  "canceled" ;
	});
	return mapJSON(data, [&](const Value row){
		Order ord;
		Value descr = row["descr"];
		ord.client_id = orderDB.getAndMark({pair,row["userref"].getIntLong()});
		ord.id = row.getKey();
		ord.price = descr["price"].getNumber();
		ord.size = row["vol"].getNumber() - row["vol_exec"].getNumber();
		if (descr["type"].getString() == "sell")
			ord.size = -ord.size;
		return ord;
	}, Orders());
}

std::string double2string(double x, unsigned int decimals) {
	std::ostringstream buff;
	buff << std::fixed << std::setprecision(decimals) << x;
	return buff.str();
}

json::Value Interface::placeOrderImp(const std::string_view &pair, double size, double price, json::Value clientId, bool lev) {

	auto symb = stripPrefix(pair);
	Value symbinfo = pairMap[symb];

	Object req;
	req.setItems({{"pair",symb},
		{"type",size<0?"sell":"buy"},
		{"ordertype","limit"},
		{"price",double2string(price, symbinfo["pair_decimals"].getUInt())},
		{"volume",double2string(std::abs(size), symbinfo["lot_decimals"].getUInt())},
		{"oflags","fciq,post"}
	});
	if (lev) {
		double levlev = symbinfo[size<0?"leverage_sell":"leverage_buy"]
				.reduce([&](double a, Value b){
			return std::max(a, b.getNumber());
		},0.0);
		req.set("leverage", levlev);
	}
	if (clientId.defined()) {
		std::hash<json::Value> h;
		std::int32_t userref = ((h(clientId) & 0x7FFFFFFE) + 1);
		Value key = {pair,userref};
		Value cc = orderDB.get(key);
		if (!cc.defined()) {
			orderDB.store(key, clientId);
		}
		req.set("userref", userref);
	}
	Value resp = private_POST("/0/private/AddOrder", req);
	return resp["result"]["txid"][0];

}

json::Value Interface::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {

	updateSymbols();

	auto symb = stripPrefix(pair);
	auto mt = getMarketType(pair);

	Value symbinfo = pairMap[symb];
	if (!symbinfo.defined()) throw std::runtime_error("unknown symbol");

	if (replaceId.defined()) {
		Value resp = private_POST("/0/private/CancelOrder", Object
				({{"txid", replaceId}}));
		if (resp["result"]["count"].getUInt() == 0) {
			return nullptr;
		}
	}




	if (size) {
		bool lev = false;
		switch (mt) {
		case MarketType::exchange: lev = false;break;
		case MarketType::leveraged: lev = true;break;
		case MarketType::hybrid: {
			double pos = getPosition(symb);
			auto base = symbinfo["base"].getString();
			auto quote = symbinfo["quote"].getString();
			/*
			 * If position is opened, and can be closed or reduced, then close or reduce position
			 * If position is not opened, and order can be placed on spot, then place order on spot
			 * If order cannot be placed on spot, thne place it using leverage
			 */
			if (size > 0) {
				if (pos < 0) {
					lev = true;
					if (pos + size > 0) size = -pos;
				} else {
					lev = getSpotBalance(quote) < size * price * 1.002;
				}
			} else {
				if (pos > 0) {
					lev = true;
					if (pos + size < 0) size = -pos;
				} else {
					lev = getSpotBalance(base) < -size;
				}
			}
			break;}
		}
		try {
			return placeOrderImp(pair, size, price, clientId, lev);
		} catch (const std::exception &e) {
			ondra_shared::StrViewA msg(e.what());
			if (msg.indexOf("Insufficient funds") != msg.npos && mt == MarketType::hybrid && !lev) {
				return placeOrderImp(pair, size, price, clientId, true);
			} else {
				throw;
			}
		}


	}


	return nullptr; //todo
}

/*
["enableDebug",true]
["getBalance",{"pair":"lv_XBTUSDT","symbol":"USDT"}]
*/
double Interface::getFees(const std::string_view &pair) {

	if (fees < 0 && hasKey()) {
		Value result = private_POST("/0/private/TradesHistory",Value());
		double bestTime = 0;
		for (Value v : result["result"]["trades"]) {
			double tm = v["time"].getNumber();
			if (tm > bestTime) {
				double cost = v["cost"].getNumber();
				Value fee = v["fee"];
				if (cost && fee.defined()) {
					bestTime = tm;
					fees = fee.getNumber()/cost;
				}
			}
		}

	}
	if (fees < 0) fees = 0.16;
	return fees;
}

IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {

	auto symb = stripPrefix(pair);

	std::ostringstream tickers;
	if (!tickerValid && tickerMap.hasValue()) {
		tickers << "/0/public/Ticker?pair=" << tickerMap.map([](Value v){return v.getKey();}).join(",").str();
		tickerMap = public_GET(tickers.str())["result"];
		tickerValid = true;
	}
	Value t = tickerMap[symb];
	if (!t.hasValue()) {
		tickers.clear();
		tickers.str("");
		tickers << "/0/public/Ticker?pair=" << symb;
		Value t = public_GET(tickers.str())["result"];
		if (!tickerMap.hasValue()) {
			tickerMap = t;
		} else {
			tickerMap = tickerMap.merge(t);
		}
	}
	t = tickerMap[symb];
	if (!t.hasValue()) throw std::runtime_error("Unknown symbol");

	return Ticker {
		t["b"][0].getNumber(),
		t["a"][0].getNumber(),
		t["c"][0].getNumber(),
		static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count())
	};

}

std::string_view Interface::stripPrefix(std::string_view pair) {
	return pair.substr(3);
}

json::Value Interface::getWallet_direct() {
	if (!positionMap.defined()) {
		positionMap = private_POST("/0/private/OpenPositions",Value());
	}
	Value pos = positionMap["result"].map([&](Value z){
		double vol =  (z["vol"].getNumber()-z["vol_closed"].getNumber())*(z["type"].getString() == "sell"?-1.0:1.0);
		String name({z["pair"].getString(),":",z.getKey()});
		return Value(name, vol);
	});


	getSpotBalance("");
	return Object({{"spot", balanceMap["result"]},{"positions", pos}});

}

json::Value Interface::getSettings(const std::string_view & pairHint) const {
	return {
		Object({
			{"name","allow_margin"},
			{"default",allow_margin?"yes":"no"},
			{"type","enum"},
			{"label","Enable Leverage"},
			{"options", Object({
				{"yes","Yes (not recommended)"},
				{"no","No"}
			})},
		}),
		Object({
			{"type","label"},
			{"label","The Kraken platform has a malfunctioning liquidation engne. Due to low liquidity, high latency, insufficient protection against large price movements and zero protection against a negative balance, it is not recommended to use the Kraken platform for leverage trading. For leverage trading, consider Binance or FTX platforms"}
		})
	};
}

json::Value Interface::setSettings(json::Value v) {
	allow_margin = v["allow_margin"].getString() == "yes";
	return v;
}

void Interface::restoreSettings(json::Value v)  {
	setSettings(v);
}

bool Interface::areMinuteDataAvailable(const std::string_view &asset,const std::string_view &currency) {
	updateSymbols();
	json::Value r = pairMap.find([&](json::Value x){
		auto asset_symbol = symbolMap[x["base"].getString()].getString();
		auto currency_symbol = symbolMap[x["quote"].getString()].getString();
		return asset == asset_symbol && currency == currency_symbol;
	});
	return r.defined();
}

uint64_t Interface::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<IHistoryDataSource::OHLC> &data) {

	updateSymbols();
	std::string_view psymb = stripPrefix(hint_pair);
	Value symbinfo = pairMap[psymb];
	if (!symbinfo.defined()) {
		json::Value r = pairMap.find([&](json::Value x){
			auto asset_symbol = symbolMap[x["base"].getString()].getString();
			auto currency_symbol = symbolMap[x["quote"].getString()].getString();
			return asset == asset_symbol && currency == currency_symbol;
		});
		if (!r.defined()) return 0;
		symbinfo = r;
	}
	auto name = symbinfo.getKey();
	time_from/=1000;
	time_to/=1000;
	int sets[] = {5,15,30,60,240,1440,10080};
	for (int curset: sets) {
		std::ostringstream buff;
		buff << "/0/public/OHLC?pair=" << name << "&since=" << time_from << "&interval=" << curset;
		int dups = curset/5;
		auto insert_val = [&](double n){
				for (int i = 0; i < dups; i++) data.push_back({n,n,n,n});
		};

		std::string url = buff.str();
		Value hdata;
		if (hDataCache[url].defined()) hdata = hDataCache[url];
		else {
			hdata=public_GET(buff.str());
			hDataCache.setItems({{url, hdata}});
		}

		std::uint64_t minDate = time_to;

		for (Value row: hdata["result"][0]) {
			auto date = row[0].getUIntLong();
			if (date >= time_from && date < time_to) {
				double o = row[1].getNumber();
				double h = row[2].getNumber();
				double l = row[3].getNumber();
				double c = row[4].getNumber();
				double m = std::sqrt(h*l);
				insert_val(o);
				insert_val(h);
				insert_val(m);
				insert_val(l);
				insert_val(c);
				if (minDate > date) minDate = date;
			}
		}
		if (!data.empty()) {
			return minDate*1000;
		}
	}
	return 0;
}
