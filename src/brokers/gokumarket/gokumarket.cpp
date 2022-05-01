#include <openssl/hmac.h>
#include <sstream>
#include <random>
#include <thread>

#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <imtjson/serializer.h>
#include <imtjson/binjson.tcc>
#include <imtjson/operations.h>
#include <shared/toString.h>
#include "../../shared/logOutput.h"
#include "gokumarket.h"

using ondra_shared::logDebug;
using ondra_shared::logNote;

using namespace json;

static std::string_view favicon(
		"iVBORw0KGgoAAAANSUhEUgAAAGUAAABlCAMAAABjot39AAAAY1BMVEU0MTYAAAUAAB8GACsXADMi"
		"ADcyAkA2AElCAVQzC0U8E0k7E01EGlBJHlRRG2BJJFhPKl1XMGRVOGFpRnZfTWhqTHR6XYR2Zn2T"
		"fJ6ViJyrnrC6r77Lwc7Z1Nzq5+v69/z9//wSyP9GAAAAAXRSTlMAQObYZgAABM9JREFUaN7tmmuD"
		"qiAQhtcLEKarmUV4gf7/rzwlFxVRULfzqffTuiKPMwPDgP38fPWVh3I8Uf4BQgItSv6SZCVo0p8g"
		"cDK8eVEUl16vPwbrEnzYU6IjnGSvvsvLSO+LLMHifnrIVaKT82VFZ/ka+53VP59eHEpFuwMMmBUX"
		"p4ps9zjog5sUnkp2YQppiLeEOcV2Q3BW5N4qMrzVnH7c4HyjNmLwLojE4E2DK802K4X+GDHN0l3y"
		"HtEiPeGFbs4IQYAQOttvY09MCRdNOWdlRWjddk39uFfF+bxkjHtAy+yYnk1F147xpxZnHUXQbCSN"
		"gX5Lifk0Kil7ztUSZLb0WXTkqxiUBNb8aRVnV2ijrA80aKEkwGqHtucXWSjQw5QJBTbPdd3HrbHT"
		"mFRB8IhxGgxhTX07xWEYwow0rXYib+I5ZTk0usVQDsFyFIJodAPDiGp8M9zQVQdeX+PHGHjiejgB"
		"NK3HMERU3e3gjALz9dCPKDkX/TAaY4tg3CprwIwCnVWXfCSUMWEFxHahu8LMKdhZ24kn5Jt2EV4U"
		"JMJafoEGxB5/aFLgTULG3poNEPgrgxObFLiY8McYGfkODAwQ3OruLXoNdCSIoNTQgNiMmVfAYjZy"
		"pMGwqkeTsa6A+D94GA1XjDFb4FCYQpRzYNBOsxlvZbxgJ412UgqzBajlTFDXt3k2Y5W4dxLUyuwj"
		"d+4ekHjxkzKN2NJyPXkjCly7jplPRUybSEFH6UyvZq3qtqRv3V0uK+0O40RexgrC7hGIw9/WHggH"
		"JTfvB6JbdUm1i0TPUdk+eYldlHwhHeuwyPQkrlSqaYYGIc2cEDPJzB4Q+ekqry7PaZR85aKIKRmA"
		"XlEnh+q7GVjRVkrfL39Iyfz/6gVcHyvK91AM0Xc42rUSoNpIsZUt7ygFzy0UuIOCNlP22HJ/U/in"
		"KTXYGheXx8rbWILJ+h2t/JeYUOwqLxtrVnbZMk1q3Ty1n/TgHuYXK49Q4FVWAMESBfSOZMkhSjDP"
		"Y2hCEQtr55j7roSkqsguFt0COQokBdz1tF2jZJZg9MLG+sIbSipSqw2NokzXiaXMX84b9IlJL39o"
		"mCecD39LStX/pw3NTi7OQkmVMGvrPhM+ku6jzlJpfjYpJ4Te9pzZfL8H8ajpvCBLnJUSlIsK1SsX"
		"MnaXnbyFE6ZTg6NSmrsMy7X+NhrQVNYvnLP2N5JmRmLFY+GO2vL1tHhFHuLRuCsJIQ9CKgiMZdUS"
		"Feisxt/GyHHFzFpl4hlA1fbD66jZMhdl8c3QynxVbbCPKdYTcJXmOQkWahS95SOep+bWc3Y1etvc"
		"SinVfQJ8z+atOV+9K6sDs6Po1PHnYuSXji+srwv0XOQ1gbEigTgletXkFLir1/X0j5pRAusev2EQ"
		"BPBSsyGZ8ZtHWWk5IZkOAbpaUDy7wvpY6j7tMQKwVlE8vCpkZ2ReT0RkgcNrBLx2Ln5fjcCp5fOU"
		"TCPPYv/Hd/0HKalHxfNrzawg2AdZLwBAGKDbva7r+zUOog07Pd/QbJP7019yHOJzOI6PQvwO+vH/"
		"gBzE+H+Fw/8DcmAIbPsAl39qCB/32p4Po8lnvTX8auCz3tLLmjcHH/oqnn/YDh0eZ3ySP/m5QrnK"
		"OX/+1x3Fz1+rzCafFGBWfn+889VXn9A/ePA5aH4495cAAAAASUVORK5CYII="

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


GokumarketIFC::GokumarketIFC(const std::string &cfg_file)
	:AbstractBrokerAPI(cfg_file, apiKeyFmt)
	,api("https://publicapi.gokumarket.com")
	,orderDB(cfg_file+".db")
{
}

IBrokerControl::BrokerInfo GokumarketIFC::getBrokerInfo() {
	return BrokerInfo {
		hasKey(),
		"gokumarket",
		"Gokumarket",
		"https://gokumarket.com/",
		"1.0",
		std::string(licence),
		std::string(favicon),
		false,
		true
	};
}


std::vector<std::string> GokumarketIFC::getAllPairs() {
	json::Value v = publicGET("/ticker", Value());
	std::vector<std::string> z;
	z.reserve(v.size());
	for (Value k: v) {
		z.push_back(k.getKey());
	}
	return z;
}

bool GokumarketIFC::areMinuteDataAvailable(const std::string_view &, const std::string_view &) {
	return false;

}

IStockApi::MarketInfo GokumarketIFC::getMarketInfo(const std::string_view &pair) {
	auto resp = publicGET("/exchange/getCurrencyPairLatestData", Object{{"currency_pair",pair}})["data"];
	MarketInfo minfo = {};
	minfo.asset_symbol = resp["first_currency"].getString();
	minfo.currency_symbol = resp["second_currency"].getString();
	double mbt = resp["min_buy_trade"].getNumber();
	double mst = resp["min_sell_trade"].getNumber();
	double mt = resp["min_trade"].getNumber();
	double curpriceUsd = resp["currentUsdPrice"].getNumber();
	double curprice = resp["currentPrice"].getNumber();
	double minvol = (std::max({mbt,mst, mt})/curpriceUsd)*curprice*1.5;
	minfo.min_volume = minvol;
	minfo.currency_step = std::pow(10,-resp["rate_precision"].getNumber());
	minfo.asset_step = std::pow(10,-resp["quantity_precision"].getNumber());
	minfo.feeScheme = FeeScheme::income;
	minfo.fees = feeRatio;
	minfo.invert_price = false;
	minfo.leverage = 0;
	minfo.min_size = minfo.asset_step;
	minfo.simulator = false;
	minfo.private_chart = false;
	minfo.wallet_id = "trade";
	return minfo;
}

AbstractBrokerAPI* GokumarketIFC::createSubaccount(
		const std::string &secure_storage_path) {
	return new GokumarketIFC(secure_storage_path);
}

void GokumarketIFC::onLoadApiKey(json::Value keyData) {
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();

}


uint64_t GokumarketIFC::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<double> &data) {
	return 0;
}


double GokumarketIFC::getBalance(const std::string_view &symb, const std::string_view &) {
	if (!balanceCache.defined()) {
		balanceCache = privateGET("/wallet/getUserBalances", Object{{"wallet_type","trade"}});
	}
	Value n = balanceCache[symb];
	if (!n.defined()) throw std::runtime_error(std::string("Balance unavailable for this symbol: ").append(symb).append(" - this pair cannot be traded, please contact Gokumarket's support!"));
	return n.getNumber() + calcLocked(symb);
}

void GokumarketIFC::onInit() {

}

IStockApi::TradesSync GokumarketIFC::syncTrades(json::Value lastId, const std::string_view &pair) {
	Value trades = fetchTrades();
	Value mytrades = trades.filter([&](Value t){
		String cp = toUpperCase(t["currency_pair"].getString());
		return cp.str() == pair;
	});
	std::hash<json::Value> h;
	Array blind;
	Value newtrades = mytrades.filter([&](Value t){
		Value hash = h(t["tradeId"]);
		blind.push_back(hash);
		return lastId.indexOf(hash) == lastId.npos;
	});
	newtrades = newtrades.reverse();
	if (lastId.hasValue()) {
		return TradesSync{
			mapJSON(newtrades, [](Value t){

				Value maker = t["maker"];
				Value taker = t["taker"];
				Value details = maker.defined()?maker:taker;
				double price = t["rate"].getNumber();
				double size = t["quantity"].getNumber();
				if (details["type"].getString() == "sell") size = -size;
				Value asset = t["split"][0];
				Value currency = t["split"][1];
				Value feesCurrency = details["feesCurrency"];
				double fees = details["fees"].getNumber();
				double eff_price = price;
				double eff_size = size;
				if (feesCurrency == asset) {
					eff_size -= fees;
				} else if (feesCurrency == currency && size) {
					eff_price += fees/size;
				}
				return Trade{
					t["tradeId"],
					t["created"].getUIntLong(),
					size,
					price,
					eff_size,
					eff_price
				};
			}, TradeHistory()), blind
		};
	} else {
		return TradesSync{ {}, blind};
	}
}

bool GokumarketIFC::reset() {
	balanceCache = Value();
	orderCache = Value();
	tradeCache = Value();
	return true;
}

IStockApi::Orders GokumarketIFC::getOpenOrders(const std::string_view &pair) {

	Value cache = fetchOpenOrders();
	Value flt = cache.filter([&](Value c){
		String s =toUpperCase(c["currency_pair"].getString());
		return s.str() == pair;
	});
	return mapJSON(flt, [&](Value v){
		return IStockApi::Order{
			v["orderId"],
			orderDB.getAndMark(v["orderId"]),
			v["remaining"].getNumber()*(v["type"].getString()=="sell"?-1:1),
			v["rate"].getNumber()
		};
	}, IStockApi::Orders());

}

json::Value GokumarketIFC::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {

	if (clientId.defined() && replaceId.defined()) {
		if (!checkRateLimit(replaceId)) return replaceId;
	}
	if (replaceId.defined()) {
		privatePOST("/exchange/cancelOrder", Object{
			{"orderId",replaceId},
			{"currency_pair", pair}
		});
	}
	if (size) {
			Value req = Object {
				{"quantity", Value(std::abs(size)).toString()},
				{"rate", Value(price).toString()},
				{"currency_pair",pair}
			};
			std::string_view uri;
			if (size<0) uri = "/exchange/placeSellLimitOrder";
			else if (size>0) uri = "/exchange/placeBuyLimitOrder";
			Value res = privatePOST(uri, req);
			orderDB.store(res, clientId);
			return res;
	} else {
		return nullptr;
	}
}


IBrokerControl::AllWallets GokumarketIFC::getWallet() {
	Value res = privateGET("/wallet/getUserBalancesV2", Value());
	AllWallets out;
	for (Value x: res) {
		Wallet w;
		w.walletId = x.getKey();
		for (Value y: x) {
			w.wallet.push_back({y.getKey(), y.getNumber()});
		}
		out.push_back(w);
	}
	return out;
}

IStockApi::Ticker GokumarketIFC::getTicker(const std::string_view &pair) {
	Value v = publicGET("/orderbooks",Object{{"currency_pair",pair},{"limit",1}});
	double bid = v["bids"][0][1].getNumber();
	double ask = v["asks"][0][1].getNumber();
	double mid = (ask+bid)*0.5;
	return IStockApi::Ticker {
		bid,ask,mid,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count())
	};
}


Value GokumarketIFC::publicGET(const std::string_view &uri, Value query) const {
	try {
		return api.GETq(uri, query);
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e,false);
		throw;
	}
}

bool GokumarketIFC::hasKey() const {
	return !( api_key.empty() || api_secret.empty());
}

Value GokumarketIFC::privateGET(const std::string_view &uri, Value query, int retries)  const{
	try {
		std::string fulluri (uri);
		HTTPJson::buildQuery(query, fulluri, "?");
		return processResponse(api.GET(fulluri,signRequest("GET", fulluri, query)));
	} catch (const HTTPJson::UnknownStatusException &e) {
		if (processError(e, retries>0)) return privateGET(uri, query, retries-1);
		throw;
	}
}

Value GokumarketIFC::privatePOST(const std::string_view &uri, Value args, int retries) const {
	try {
		return processResponse(api.POST(uri, args, signRequest("POST", uri, args)));
	} catch (const HTTPJson::UnknownStatusException &e) {
		if (processError(e,retries>0)) return privatePOST(uri, args, retries-1);
		throw;
	}
}

Value GokumarketIFC::privateDELETE(const std::string_view &uri, Value query) const {
	try {
		std::string fulluri (uri);
		HTTPJson::buildQuery(query, fulluri, "?");
		return processResponse(api.DELETE(fulluri,Value(),signRequest("DELETE", fulluri, Value())));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e, false);
		throw;
	}
}

Value GokumarketIFC::signRequest(const std::string_view &method, const std::string_view &function, json::Value args) const {
	auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count();
	args.setItems({
		{"X-NONCE",timestamp},
		{"X-RECV-WINDOW",5000},
		{"X-REQUEST-URL",function}
	});
	std::string digest = args.stringify().str();
	unsigned char sign[256];
	unsigned int signLen(sizeof(sign));
	HMAC(EVP_sha256(), api_secret.data(), api_secret.length(),
			reinterpret_cast<const unsigned char *>(digest.data()), digest.length(), sign, &signLen);
	json::String hexDigest(signLen*2,[&](char *c){
		const char *hexletters = "0123456789abcdef";
		char *d = c;
		for (unsigned int i = 0; i < signLen; i++) {
			*d++ = hexletters[sign[i] >> 4];
			*d++ = hexletters[sign[i] & 0xf];
		}
		return d-c;
	});

	Value s =  Object{
		{"X-NONCE",timestamp},
		{"X-RECV-WINDOW",5000},
		{"X-API-KEY",api_key},
		{"X-SIGNATURE",hexDigest},
	};
	logDebug("SIGN: $1", s.toString().str());
	return s;

}


bool GokumarketIFC::processError(const HTTPJson::UnknownStatusException &e, bool canRetry) const {
	std::ostringstream buff;
	buff << e.code << " " << e.message;
	if (e.body.defined()) {
		buff <<  " - " <<  e.body["message"].getString();
	}
	if (e.code == 429 && canRetry) {
		int rt = e.headers["Retry-After"].getInt();
		if (rt > 0 && rt < 15) {
			logNote("Waiting $1 seconds", rt);
			for (int i = 0; i < rt; i+=2) {
				logNote("need more time");
				std::this_thread::sleep_for(std::chrono::seconds(2));
			}
		}
		return true;
	}
	throw std::runtime_error(buff.str());
	return false;
}

double GokumarketIFC::getFees(const std::string_view &pair) {
	return feeRatio;
}



json::Value GokumarketIFC::processResponse(json::Value v) const {
	return v["data"];

}

json::Value GokumarketIFC::testCall(const std::string_view &method, json::Value args) {
	Value uri = args[0];
	Value data = args[1];
	if (method == "GET") return privateGET(uri.getString(),data);
	else if (method == "POST") return privatePOST(uri.getString(),data);
	else return nullptr;

}

json::Value GokumarketIFC::fetchOpenOrders() {
	if (orderCache.defined()) return orderCache;
	orderCache = privateGET("/exchange/getUserOpenOrders",Value());
	return orderCache;
}

double GokumarketIFC::calcLocked(const std::string_view &symbol) {
	Value orders = fetchOpenOrders();
	double sum = 0;
	for (Value ord: orders) {
		String symb1 = toUpperCase(ord["split"][0].getString());
		String symb2 = toUpperCase(ord["split"][1].getString());
		if (symb1.str() == symbol) {
			auto type = ord["type"].getString();
			if (type == "sell") {
				sum += ord["remaining"].getNumber();
			}
		} else if (symb2.str() == symbol) {
			auto type = ord["type"].getString();
			if (type == "buy") {
				sum += ord["remaining"].getNumber() * ord["rate"].getNumber();
			}
		}
	}
	logDebug("Symbol locked: $1 = $2", symbol, sum);
	return sum;
}

String GokumarketIFC::toUpperCase(std::string_view x) {
	return String(x.length(),[&](char *c){
		char *d = c;
		for (char z: x) *d++ = std::toupper(z);
		return d - c;
	});
}

json::Value GokumarketIFC::fetchTrades() {
	if (tradeCache.defined()) return tradeCache;
	tradeCache = privateGET("/exchange/getUserTrades",Object{{"limit","20"}});
	if (!tradeCache.empty()) {
		Value t = tradeCache[0];
			Value maker = t["maker"];
			Value taker = t["taker"];
			Value details = maker.defined()?maker:taker;
			feeRatio = details["feesPercentage"].getNumber()*0.01;
		}
	return tradeCache;

}

bool GokumarketIFC::checkRateLimit(json::Value cancelingOrder) {
	json::Value orders = fetchOpenOrders();
	json::Value o = orders.find([&](Value x){return x["orderId"] == cancelingOrder;});
	if (o.defined()) {
		std::uint64_t created = o["created"].getUIntLong();
		std::uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count();
		bool res = created+(200*1000) < now;
		logDebug("Rate limit for order $1: $2",cancelingOrder.toString().str(), res?"OK":"Rejected");
		return res;
	} else {
		return true;
	}
}
