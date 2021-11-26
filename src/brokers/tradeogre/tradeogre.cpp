#include "tradeogre.h"

#include <bits/stdint-uintn.h>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <imtjson/binary.h>
#include <imtjson/ivalue.h>
#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <imtjson/serializer.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>
#include "../../main/ibrokercontrol.h"
#include "../../main/istockapi.h"
#include "../../server/src/simpleServer/abstractStream.h"
#include "../../server/src/simpleServer/exceptions.h"
#include "../../server/src/simpleServer/http_client.h"
#include "../../server/src/simpleServer/urlencode.h"
#include "../../shared/logOutput.h"

using ondra_shared::logDebug;

using namespace json;

static std::string_view favicon(
		"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAM1BMVEVAAAAfIR80o2wsp2wxpmYy"
		"qGcvqW4zqWgnrWo1qmktrWQqr2w3rGsxr2cusW4ws3Aut2Y7PS9TAAAAAXRSTlMAQObYZgAAA9lJ"
		"REFUeNrtW9ty4zAI1ZTBI2YUuv//tbvtpqmdiKtw89DwlowtjjjcjOXWXvKSXyq8/0F4E2buPwKA"
		"jj8vjDshotMB4CMkPArxuQCwmRDwRAgD+xzWnWxwmgHG3DUeIACe44OIIrR7oZMASATzIwScA11w"
		"EQbt9kcAAHOu0imDhW2JnoAwNRNk+UEdQAOLBp0ejwcYFM4QsEASJw0gBKLiijca+vH/kdOPeo7Z"
		"FAT3/0cRbC72Zja43uMJU0W687YpAJiDyzBg30VTCHQtJmkAAfdFBQFn9XMAAGkIdkbgnAEc96Eg"
		"fMgWlNTvAMASgvGFAKxwVlf05kyFhz8jZACO+i6gLKlGLNzzKQDixZgTTSehKokaGK1jqn6IROCG"
		"KQAMBgRMxF/EggYA70ZGmkJChwBhbpVWheATRDyflQL4yI/MMf3VAD4z5ENuXg1jzEln5vdnAviO"
		"jucBgBoADeHJAOYt+o8CSPJQCaA90QmT+eBmAV6Pgp5E8B8AVYQhTx9KbTHJc5N/LTQUikhqBQAi"
		"q82XX6Pg8VEg4AJo55BQ9FEUgd1RQbAfCyKwGbB6OWEMUJaGTACZTd1d21cAkMhrURBmhkR+EjwX"
		"xutPoDbZbAGHDdD91dHDFbtmaXOTVXiAbgGLsgIPUH1gs65nx8rksZLfAcjfoLCretMCAUaV8WWL"
		"HtPP/jbRGSi8QoDSnbir5hIB8gjNrR/WCJC8YHg7WDkLuP1l0mt8vfXu4kTJwYDfWqKjDIFEJkcQ"
		"BtyFBP2bNuYnA0AkXmgeJ2y5PQ95UxDJWHOUrtTHvE7A3gK7IxeUrb+i9wwzYe0OW/S1kSZnSgZF"
		"HqFbsg2VDNYjPYKNgJZmCK5eWTXotqL/4nxaUCa6La9/xJ/anQR4XkhxSL28aM8YgP5JfIxYRkBy"
		"fIhuArbywZm8rVQE5vRfyiKAoc4AqQEO1+nnUA1a0s91KSgVAX25C1p0QfdK1E5hoLIGUJn+niLg"
		"XVChvld078RxMkDcY1R/y9VA6U2Wfe7EA8BRg0EutRTsR1MG2DQ/o1hYp5oQnWcKGTX/GsU/noNW"
		"C8AO9ciSd9e+Lb7BmwzIKLSdhQjYO1vgzFwYALjK7ZaqqrAUAUeF4MzprS4C7pZwRnTwgF9k/Og8"
		"ZlVdhMNH3MqbgDMBEJ4JgGr05wFUNWGXJACu0R8/a+99DjlJ/W3hdfXZL6M87xBN/bDwxYvDdObc"
		"aelbJDsCDdpXv86zHICqnW4CQGnZUNLPXPUFlkYAC297Sj/Dkwmg41MjjUF0aeUipaDvzuuNmcd5"
		"3x6S4Hofh0LbS17yG+QvVt+SLzziItEAAAAASUVORK5CYII="
);

static std::string_view licence(
		R"mit(Copyright (c) 2019 Ondřej Novák

		Permission is hereby granted, free of charge, to any person
		obtaining a copy of this software and associated documentation
		files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use,
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
		OTHER DEALINGS IN THE SOFTWARE.)mit"
);

static Value apiKeyFmt ({
		Object{{"name","key"},{"label","Key"},{"type","string"}},
		Object{{"name","secret"},{"label","Secret"},{"type","string"}}
});

static std::pair<std::string_view,std::string_view> extractSymbols(std::string_view market) {
	auto pos = market.find('-');
	if (pos == market.npos) throw std::runtime_error("Unknown symbol");
	return {market.substr(pos+1),market.substr(0,pos)};
}


TradeOgreIFC::TradeOgreIFC(const std::string &cfg_file)
	:AbstractBrokerAPI(cfg_file, apiKeyFmt)
	,api(simpleServer::HttpClient("mmbot (+https://www.mmbot.trade)",simpleServer::newHttpsProvider(), 0, simpleServer::newCachedDNSProvider(15)),"https://tradeogre.com/api")
	,orderDB(cfg_file+".db", 1000)
{
	api.setForceJSON(true);
}

IStockApi::BrokerInfo TradeOgreIFC::getBrokerInfo() {
	return BrokerInfo {
		hasKey(),
		"tradeogre",
		"TradeOgre",
		"https://tradeogre.com/",
		"1.0",
		std::string(licence),
		std::string(favicon),
		false,
		true
	};
}


std::vector<std::string> TradeOgreIFC::getAllPairs() {
	updateSymbols();
	std::vector<std::string> out;
	out.reserve(symbolMap.size());
	for (const auto &x: symbolMap) out.push_back(x.first);
	return out;
}

IStockApi::MarketInfo TradeOgreIFC::getMarketInfo(const std::string_view &pair) {
	const auto &s = findSymbol(pair);
	return s;

}

AbstractBrokerAPI* TradeOgreIFC::createSubaccount(
		const std::string &secure_storage_path) {
	return new TradeOgreIFC(secure_storage_path);
}

void TradeOgreIFC::onLoadApiKey(json::Value keyData) {
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();

	std::string basic = api_key + ":" + api_secret;
	json::String val({"Basic ",
		base64->encodeBinaryValue(
				json::BinaryView(reinterpret_cast<const unsigned char *>(basic.data()), basic.size())).getString()});
	api_hdr = Object{
		{"Authorization",val}
	};

}

json::Value TradeOgreIFC::getMarkets() const {
	updateSymbols();
	std::map<std::string_view, Object> smap;
	for(const auto &x: symbolMap) {
		smap[x.second.asset_symbol].set(x.second.currency_symbol, x.first);
	}
	return Object{
		{"Spot",Value(object, smap.begin(), smap.end(),[](const auto &x){
			return Value(x.first, x.second);
		})}
	};
}

double TradeOgreIFC::getBalance(const std::string_view &symb, const std::string_view &) {
	auto p = balanceCache.find(symb);
	if (p == balanceCache.end()) return 0;
	return p->second;
}

void TradeOgreIFC::onInit() {
	reset();
}

IStockApi::TradesSync TradeOgreIFC::syncTrades(json::Value lastId, const std::string_view &pair) {


	Array mostIDS;
	std::uint64_t mostTime = 0;
	auto findMostTime = [&](Value fills) {
		for (Value f: fills) {
			std::uint64_t t = f["date"].getUIntLong()*1000;
			if (mostTime <=t) {
				if (mostTime < t) mostIDS.clear();
				std::hash<json::Value> hh;
				Value hash = hh(f);
				mostIDS.push_back(hash);
				mostTime = t;
			}
		}
	};

	if (lastId.hasValue()) {

		Value jfrom = lastId[0];
		Value jlist = lastId[1];
		if (jlist.type() != json::array) {
			jfrom = 0;
			jlist = lastId;
		}


		Value &hist = historyCache[std::string(pair)];
		if (!hist.defined()) hist = privateGET(std::string("/v1/account/history/").append(pair),Value())["data"];
		std::uint64_t from = jfrom.getUIntLong();
		Value fills = hist.filter([&](Value row){
			std::uint64_t time = row["date"].getUIntLong()*1000;
			return time >= from;
		});
		if (fills.empty()) {
			return {{},lastId};
		}

		findMostTime(fills);
		Value ffils = fills.filter([&](Value r){
			std::hash<json::Value> hh;
			Value hash = hh(r);
			return jlist.indexOf(hash) == Value::npos;
		});
		if (!fills.empty() && ffils.empty()) {
			return {{},{mostTime+1, mostIDS}};
		}

		return TradesSync{mapJSON(ffils, [](Value rw){
			std::hash<json::Value> hh;
			Value hash = hh(rw);
			std::uint64_t time = rw["date"].getUIntLong()*1000;
			double price = rw["price"].getNumber();
			int dir = rw["type"].getString() == "sell"?-1:1;
			double size = rw["quantity"].getNumber()*dir;
			double eff_price = price;
			double eff_size = size;
			if (dir > 0) {
				eff_price = price * 1.002;
			} else {
				eff_price = price / 1.002;
			}
			return Trade {
				hash, time, size, price, eff_size, eff_price
			};
		}, TradeHistory()), {mostTime, mostIDS}};
	} else {
		Value fills = privateGET(std::string("/v1/account/history/").append(pair),Value())["data"];
		findMostTime(fills);
		if (mostTime == 0) mostTime = std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count();
		return TradesSync{{},{mostTime, mostIDS}};

	}
}

bool TradeOgreIFC::reset() {
	if (hasKey()) {
		Value bals = privateGET("/v1/account/balances",Value())["balances"];
		std::set<std::string, std::less<> > changeSymbols;
		for (Value x: bals) {
			std::string symb = x.getKey();
			double val = x.getNumber();
			double prev_val = getBalance(symb, symb);
			if (val != prev_val) changeSymbols.insert(symb);
			balanceCache[symb] = val;
		}
		for (auto iter = historyCache.begin(); iter != historyCache.end();++iter) {
			auto x = changeSymbols.find(extractSymbols(iter->first).first);
			if (x != changeSymbols.end()) {
				iter->second = Value();
				logDebug("Reset trading history cache for: $1", iter->first);
			}
		}
	}
	orderCache = Value();
	return true;
}

IStockApi::Orders TradeOgreIFC::getOpenOrders(const std::string_view &pair) {
	if (!orderCache.defined())
		orderCache = privatePOST("/v1/account/orders",Value());
	Value orders = orderCache.filter([&](Value rw){
			return rw["market"].getString() == pair;
	});
	return mapJSON(orders,[&](Value rw){
		Value uuid = rw["uuid"];
		Value clientId = orderDB.getAndMark(uuid);
		double price = rw["price"].getNumber();
		double size = rw["quantity"].getNumber();
		int dir = rw["type"].getString() == "sell"?-1:1;
		return (Order{
			uuid,
			clientId,
			size *dir,
			price
		});
	}, Orders());
}

json::Value TradeOgreIFC::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {

	if (replaceId.hasValue()) {
		Value res = privatePOST("/v1/order/cancel", Object{{"uuid", replaceId}});
		if (!res["success"].getBool()) return nullptr;
	}
	if (size!=0) {
		double sz = std::abs(size);
		int dir = size<0?-1:1;
		Value ordst = dir>0
				?privatePOST("/v1/order/buy",Object{{"market", pair},{"quantity",sz},{"price",price}})
				:privatePOST("/v1/order/sell",Object{{"market", pair},{"quantity",sz},{"price",price}});
		std::string uuid = ordst["uuid"].getString();
		if (ordst["success"].getBool() == false) {
			if (ordst["error"].getString() == "Please wait") {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				return placeOrder(pair, size, price, clientId, replaceId, replaceSize);
			} else {
				throw std::runtime_error(ordst["error"].getString());
			}
		}
		std::this_thread::sleep_for(std::chrono::seconds(1)); /// < probably need to wait 1 second
		if (!uuid.empty()) {
			Value uuid = ordst["uuid"];
			if (clientId.defined()) orderDB.store(uuid, clientId);
			return uuid;
		} else {
			return nullptr;
		}

	}

	return nullptr;
}


IBrokerControl::AllWallets TradeOgreIFC::getWallet() {
	Value v = privateGET("/v1/account/balances", Value());
	Wallet w;
	w.walletId="spot";
	for (const auto &x : v["balances"]) {
		w.wallet.push_back({x.getKey(), x.getNumber()});
	}
	return {w};
}

IStockApi::Ticker TradeOgreIFC::getTicker(const std::string_view &pair) {
	json::Value res = publicGET(std::string("/v1/ticker/").append(pair),Value());
	return IStockApi::Ticker {
		res["bid"].getNumber(),
		res["ask"].getNumber(),
		res["price"].getNumber(),
		static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count())
	};
}


Value TradeOgreIFC::publicGET(const std::string_view &uri, Value query) const {
	try {
		return api.GET(buildUri(uri, query));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

const std::string& TradeOgreIFC::buildUri(const std::string_view &uri, Value query) const {
	uriBuffer.clear();
	uriBuffer.append(uri);
	char c='?';
	for (Value v: query) {
		uriBuffer.push_back(c);
		uriBuffer.append(v.getKey());
		uriBuffer.push_back('=');
		if (v.type() == json::number && (v.flags() & json::numberInteger) == 0) {
			char buff[200];
			snprintf(buff,200,"%.8f", v.getNumber());
			uriBuffer.append(buff);
		} else {
			simpleServer::urlEncoder([&](char x){uriBuffer.push_back(x);})(v.toString());
		}
		c = '&';
	}
	return uriBuffer;
}

bool TradeOgreIFC::hasKey() const {
	return !(api_key.empty() || api_secret.empty());
}

Value TradeOgreIFC::privateGET(const std::string_view &uri, Value query)  const{
	try {
		std::string fulluri = buildUri(uri, query);
		Value hdr = api_hdr;
		return api.GET(fulluri,std::move(hdr));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

Value TradeOgreIFC::privatePOST(const std::string_view &uri, Value args) const {
	try {
		std::string strbody = buildUri("",args);
		Value body(std::string_view(strbody).substr(std::min<std::string::size_type>(1,strbody.size())));
		Value hdr = api_hdr;
		return api.POST(uri, body,std::move(hdr));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}



void TradeOgreIFC::processError(const HTTPJson::UnknownStatusException &e) const {
	std::ostringstream buff;
	buff << e.getStatusCode() << " " << e.getStatusMessage();
	try {
		auto s = e.response.getBody();
		json::Value error = json::Value::parse(s);
		buff <<  " - " << error["code"].getUInt() << " " << error["msg"].getString();
	} catch (...) {

	}
	throw std::runtime_error(buff.str());
}

double TradeOgreIFC::getFees(const std::string_view &pair) {
	return getMarketInfo(pair).fees;
}

void TradeOgreIFC::updateSymbols() const {
	auto now = api.now();
	if (symbolExpires <= now) {

		Value symblst = publicGET("/v1/markets", Value());
		SymbolMap smb;

		for (Value s: symblst) {
			Value g = s[0];
			auto symbol = g.getKey();
			auto exs = extractSymbols(symbol);
			MarketInfoEx nfo;
			nfo.asset_step = 0.00000001;
			nfo.currency_step = 0.00000001;
			nfo.asset_symbol= exs.first;
			nfo.currency_symbol = exs.second;
			nfo.feeScheme = currency;
			nfo.fees = 0.002;
			nfo.invert_price = false;
			nfo.leverage = 0;
			nfo.min_size = 0.00000001;
			nfo.min_volume = 0.00000001;
			nfo.private_chart = false;
			nfo.simulator = false;
			nfo.wallet_id = "";
			smb.emplace(std::string(symbol), std::move(nfo));

		}


		symbolMap = std::move(smb);

		symbolExpires = now +std::chrono::hours(1);
	}
}

const TradeOgreIFC::MarketInfoEx& TradeOgreIFC::findSymbol(const std::string_view &name) const {
	updateSymbols();
	auto iter = symbolMap.find(name);
	if (iter == symbolMap.end()) throw std::runtime_error("Unknown symbol");
	return iter->second;
}



