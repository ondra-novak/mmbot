/*
 * interface.cpp
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */
#include <sstream>

#include "interface.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <cmath>

#include <imtjson/object.h>
#include <simpleServer/http_client.h>
#include "../isotime.h"
#include <imtjson/operations.h>
#include <imtjson/parser.h>
#include <imtjson/string.h>
#include <imtjson/value.h>
#include <simpleServer/urlencode.h>
#include <shared/logOutput.h>
#include <shared/stringview.h>
using json::Object;
using json::String;
using json::Value;
using ondra_shared::logDebug;

static const StrViewA userAgent("+https://mmbot.trade");

Interface::Interface(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path,
			{
						Object
							("name","key")
							("label","API Key")
							("type","string"),
						Object
							("name","secret")
							("label","API Secret")
							("type","string"),
						Object
							("name","subaccount")
							("label","Subaccount (optional)")
							("type","string")
			}),
	api(simpleServer::HttpClient(userAgent,simpleServer::newHttpsProvider(), nullptr,simpleServer::newCachedDNSProvider(60)),
			"https://ftx.com/api")
{
	auto nonce = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*10;
	order_nonce = nonce & 0xFFFFFFF;
}


void Interface::updatePairs() {
	Value mres = api.GET("/markets");
	Value result = mres["result"];
	const AccountInfo &account = getAccountInfo();
	SymbolMap::Set::VecT newsmap;
	for (Value symbol: result) {
		if (symbol["enabled"].getBool()) {
			MarketInfo minfo;
			String name = symbol["name"].toString();
			StrViewA type = symbol["type"].getString();
			bool isfuture;
			if (type == "future") isfuture = true;
			else if (type == "spot") isfuture = false;
			else continue;

			minfo.asset_step = symbol["sizeIncrement"].getNumber();
			minfo.asset_symbol = isfuture?symbol["underlying"].getString():symbol["baseCurrency"].getString();
			minfo.currency_step = symbol["priceIncrement"].getNumber();
			minfo.currency_symbol = isfuture?StrViewA("USD"):symbol["quoteCurrency"].getString();
			minfo.feeScheme = isfuture?currency:income;
			minfo.fees = account.fees;
			minfo.invert_price = false;
			minfo.leverage = isfuture?account.leverage:0;
			minfo.min_size = symbol["minProvideSize"].getNumber();
			minfo.min_volume = 0;
			newsmap.emplace_back(name.str(), std::move(minfo));
		}
	}
	smap.swap(newsmap);
}


std::vector<std::string> Interface::getAllPairs() {
	updatePairs();
	std::vector<std::string> out;
	std::transform(smap.begin(), smap.end(), std::back_inserter(out), [&](const auto &itm){
		return itm.first;
	});
	return out;
}

IStockApi::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	if (smap.empty()) updatePairs();
	auto iter = smap.find(pair);
	if (iter == smap.end())
		throw std::runtime_error("Unknown symbol");
	return iter->second;
}


AbstractBrokerAPI* Interface::createSubaccount(
				const std::string &secure_storage_path) {
	return new Interface(secure_storage_path);
}


IStockApi::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		hasKey(),
		"ftx",
		"FTX",
		"https://ftx.com/#a=3140432",
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
"iVBORw0KGgoAAAANSUhEUgAAAEAAAABAAgMAAADXB5lNAAAADFBMVEUAAAACpsJfyt6r6/RmPlXo"
"AAAAAXRSTlMAQObYZgAAAClJREFUOMtjYKAZCIWBkBEpQHWgtWrVglUQMAIEaAb+//9/YOQI0BoA"
"APLVxM4kiXyyAAAAAElFTkSuQmCC",
false,true
	};
}

void Interface::onLoadApiKey(json::Value keyData) {
	api_key=keyData["key"].getString();
	api_secret=keyData["secret"].getString();
	api_subaccount=keyData["subaccount"].getString();
}

double Interface::getBalance(const std::string_view &symb, const std::string_view &pair) {
	if (smap.empty()) updatePairs();
	auto iter = smap.find(pair);
	if (iter == smap.end())
		throw std::runtime_error("Unknown symbol");
	if (iter->second.leverage) {
		const AccountInfo &acc = getAccountInfo();
		if (symb == iter->second.currency_symbol) return getAccountInfo().colateral;
		else {
			auto iter = acc.positions.find(pair);
			if (iter == acc.positions.end()) return 0.0;
			else return iter->second;
		}
	} else {
		if (balances.empty()) {
			Value resp = requestGET("/wallet/balances");
			if (resp["success"].getBool()) {
				resp = resp["result"];
				BalanceMap::Set::VecT bmap;
				for (Value r: resp) {
					String symbol = r["coin"].toString();
					double val = r["total"].getNumber();
					bmap.emplace_back(symbol.str(), val);
				}
				balances.swap(bmap);
			} else {
				throw std::runtime_error("Failed to get balances");
			}
		}
		auto iter = balances.find(symb);
		if (iter == balances.end()) return 0.0;
		else return iter->second;
	}
}


IStockApi::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	std::ostringstream uri;
	uri << "/fills?market=" << simpleServer::urlEncode(pair);
	if (lastId.hasValue()) {
		auto start_time = lastId[0].getUIntLong()/1000;
		uri << "&limit=100"
				<< "&start_time=" << start_time;
	} else {
		uri << "&limit=1";
	}
	json::Value resp = requestGET(uri.str());
	if (resp["success"].getBool()) {
		json::Value result = resp["result"];

		if (!lastId.hasValue()) {
				if (result.empty()) {
					return TradesSync { {}, json::array };
			} else
				return TradesSync { {}, {
						parseTime(result[0]["time"].toString(), ParseTimeFormat::iso_tm),
						result[0]["id"]
				} };
		} else {

			std::uint64_t topDate = lastId[0].getUIntLong();
			Value topId;
			TradesSync out;
			bool fltout = false;
			for (Value v: result) {
				Value id =  v["id"];
				uint64_t date = parseTime(v["time"].toString(), ParseTimeFormat::iso_tm);
				if (date > topDate || (date == topDate && Value::compare(id, topId) > 0)) {
					topDate = date;
					topId = id;
				}
				if (id.defined() && lastId[1].defined() && Value::compare(id, lastId[1]) <= 0) {
					fltout = true;
					continue;
				}
				double side = v["side"].getString() == "buy"?1:-1;
				double size = side*v["size"].getNumber();
				double price = v["price"].getNumber();
				double eff_size = size;
				double eff_price = price;
				double fee = v["fee"].getNumber();
				if (v["feeCurrency"] == v["baseCurrency"]) {
					eff_size -= side*fee;
				} else {
					eff_price += fee/size;
				}
				out.trades.push_back({
					id,
					date,
					size,
					price,
					eff_size,
					eff_price
				});
			}

			std::reverse(out.trades.begin(), out.trades.end());

			if (out.trades.empty() && fltout) {
				out.lastId = {topDate+1000, topId};
			} else {
				out.lastId = {topDate, topId};
			}
			return out;
		}
	} else {
		throw std::runtime_error("Unable to receive trades");
	}
}

void Interface::onInit() {
}

bool Interface::reset() {
	balances.clear();
	curAccount.reset();
	return true;
}

Value Interface::parseClientId(Value v) {
	if (v.type() == json::string) return Value::fromString(v.getString())[0];
	else return Value();
}

Value Interface::buildClientId(Value v) {
	if (v.defined()) return Value({v, genOrderNonce()}).stringify();
	else return v;
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {
	std::ostringstream uri;
	uri << "/orders?market=" << simpleServer::urlEncode(pair);
	json::Value resp = requestGET(uri.str());
	if (resp["success"].getBool()) {
		return mapJSON(resp["result"],[](Value v){

			return Order{
				v["id"],
				parseClientId(v["clientId"]),
				(v["side"].getString() == "sell"?-1:1) * v["remainingSize"].getNumber(),
				v["price"].getNumber()
			};

		}, IStockApi::Orders());
	} else {
		throw std::runtime_error(resp.stringify().str());
	}
}

std::string numberToFixed(double numb, int fx) {
	std::ostringstream str;
	str.precision(fx);
	str.setf(std::ios_base::fixed);
	str << numb;
	return str.str();
}

json::Value Interface::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	try {
		std::ostringstream uri;
		if (replaceId.hasValue()) {
			if (size == 0) {
				uri << "/orders/" << simpleServer::urlEncode(replaceId.toString());
				Value resp = requestDELETE(uri.str());
				if (resp["success"].getBool()) {
					return nullptr;
				} else {
					throw std::runtime_error(resp.stringify().str());
				}
			} else {
				Value req = Object
						("size", std::abs(size))
						("price", price)
						("clientId", buildClientId(clientId));
				uri << "/orders/" << simpleServer::urlEncode(replaceId.toString()) << "/modify";
				Value resp = requestPOST(uri.str(), req);
				if (resp["success"].getBool()) {
					return resp["result"]["id"];
				} else {
					throw std::runtime_error(resp.stringify().str());
				}
			}
		}
		if (size) {
			Value req = Object
					("market", pair)
					("side", size > 0?"buy":"sell")
					("price", price)
					("type","limit")
					("size", std::abs(size))
					("postOnly",true)
					("clientId", buildClientId(clientId));
			Value resp = requestPOST("/orders", req);
			if (resp["success"].getBool()) {
				return resp["result"]["id"];
			} else {
				throw std::runtime_error(resp.stringify().str());
			}
		}
		return nullptr;
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v.toString().str());
		} catch (...) {
			throw;
		}
	}
}

double Interface::getFees(const std::string_view &pair) {
	const AccountInfo &account = getAccountInfo();
	return account.fees;
}


IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {
	std::ostringstream uri;
	uri << "/markets/" << simpleServer::urlEncode(pair) << "/orderbook?depth=1";
	json::Value resp = api.GET(uri.str());
	if (resp["success"].getBool()) {
		auto result = resp["result"];
		IStockApi::Ticker tkr;
		tkr.ask = result["asks"][0][0].getNumber();
		tkr.bid = result["bids"][0][0].getNumber();
		tkr.last = (tkr.ask + tkr.bid)*0.5;
		tkr.time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*1000;
		return tkr;
	} else {
		throw std::runtime_error(resp.stringify().str());
	}

}


bool Interface::hasKey() const {
	return !api_key.empty() && !api_secret.empty();
}


const Interface::AccountInfo& Interface::getAccountInfo() {
	if (!curAccount.has_value()) {
		if (hasKey()) {
			Value req = requestGET("/account");
			AccountInfo nfo;
			req = req["result"];
			nfo.colateral = req["totalAccountValue"].getNumber();
			nfo.fees = req["makerFee"].getNumber();
			nfo.leverage = req["leverage"].getNumber();
			Positions::Set::VecT poslist = mapJSON(req["positions"], [](Value v){
				return Positions::value_type(v["future"].getString(), v["netSize"].getNumber());
			},Positions::Set::VecT());
			nfo.positions.swap(poslist);
			curAccount = std::move(nfo);
		} else {
			AccountInfo nfo;
			nfo.colateral = 10000;
			nfo.fees = 0.002;
			nfo.leverage=20;
			nfo.positions = {};
			curAccount = std::move(nfo);
		}
	}
	return *curAccount;
}


json::Value Interface::requestGET(std::string_view path) {
	try {
		return api.GET(path, signHeaders("GET",path, Value()));
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v["error"].toString().str());
		} catch (...) {

		}
		throw;
	}
}

json::Value Interface::requestPOST(std::string_view path, json::Value params) {
	try {
		return api.POST(path, params, signHeaders("POST",path, params));
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v["error"].toString().str());
		} catch (...) {
			throw;
		}
	}
}


json::Value Interface::requestDELETE(std::string_view path) {
	try {
		return api.DELETE(path, Value(), signHeaders("DELETE",path, Value()));
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v["error"].toString().str());
		} catch (...) {
			throw;
		}
	}
}

json::Value Interface::signHeaders(const std::string_view &method, const std::string_view &path, const Value &body) {
	if (api_key.empty() || api_secret.empty()) throw std::runtime_error("Need API key");

	std::ostringstream buff;
	auto ts = now();
	buff << ts << method << "/api" << path;
	if (body.defined()) body.toStream(buff);
	std::string msg = buff.str();

	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);
	HMAC(EVP_sha256(),api_secret.data(), api_key.length(), reinterpret_cast<const unsigned char *>(msg.data()), msg.length(), digest, &digest_len);
	json::String hexDigest(digest_len*2,[&](char *c){
		const char *hexletters = "0123456789abcdef";
		char *d = c;
		for (unsigned int i = 0; i < digest_len; i++) {
			*d++ = hexletters[digest[i] >> 4];
			*d++ = hexletters[digest[i] & 0xf];
		}
		return d-c;
	});
	Value req =  Object
			("FTX-KEY", api_key)
			("FTX-TS", ts)
			("FTX-SIGN", hexDigest)
			("FTX-SUBACCOUNT", api_subaccount.empty()?Value():Value(api_subaccount));

	logDebug("$1", req.toString());
	return req;



}

std::int64_t Interface::now() {
	auto n =  std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::steady_clock::now().time_since_epoch()
						 ).count() + this->time_diff;
	if (n > time_sync) {
		HTTPJson timeapi(simpleServer::HttpClient(userAgent,simpleServer::newHttpsProvider(), nullptr,simpleServer::newCachedDNSProvider(60)),
				"https://otc.ftx.com/api");
		Value resp = timeapi.GET("/time");
		if (resp["success"].getBool()) {
			n = parseTime(resp["result"].toString(), ParseTimeFormat::iso_tm);
			time_sync = n + (3600*1000); //- one hour
			setTime(n);
		}
	}
	return n;

}

void Interface::setTime(std::int64_t t ) {
	this->time_diff = 0;
	auto n = now();
	this->time_diff = t - n;
}


int Interface::genOrderNonce() {
	order_nonce = (order_nonce+1) & 0xFFFFFFF;
	return order_nonce;
}
