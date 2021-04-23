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
#include "shared/worker.h"
using json::Object;
using json::String;
using json::Value;
using ondra_shared::logDebug;
using ondra_shared::logError;

//static ondra_shared::Worker worker;



static const StrViewA userAgent("+https://mmbot.trade");

Interface::Connection::Connection()
	:api(simpleServer::HttpClient(userAgent,simpleServer::newHttpsProvider(), nullptr,simpleServer::newCachedDNSProvider(60)),
		"https://ftx.com/api") {
	auto nonce = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*10;
	order_nonce = nonce & 0xFFFFFFF;

}

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
			})
{
	connection=PConnection::make();

}


void Interface::updatePairs() {
	Value mres = publicGET("/markets");
	Value result = mres["result"];
	const AccountInfo &account = getAccountInfo();
	SymbolMap::Set::VecT newsmap;
	for (Value symbol: result) {
		if (symbol["enabled"].getBool() && symbol["type"] == "spot") {
			MarketInfoEx minfo;
			String name = symbol["name"].toString();

			minfo.asset_step = symbol["sizeIncrement"].getNumber();
			minfo.asset_symbol = symbol["baseCurrency"].getString();
			minfo.currency_step = symbol["priceIncrement"].getNumber();
			minfo.currency_symbol = symbol["quoteCurrency"].getString();
			minfo.feeScheme = income;
			minfo.fees = account.fees;
			minfo.invert_price = false;
			minfo.leverage = 0;
			minfo.min_size = symbol["minProvideSize"].getNumber();
			minfo.min_volume = 0;
			minfo.wallet_id = "spot";
			newsmap.emplace_back(name.str(), std::move(minfo));
		}
	}
	mres = publicGET("/futures");
	result = mres["result"];
	for (Value symbol: result) {
		if (symbol["enabled"].getBool() && !symbol["expired"].getBool()) {
			MarketInfoEx minfo;
			String name = symbol["name"].toString();

			minfo.asset_step = symbol["sizeIncrement"].getNumber();
			minfo.asset_symbol = symbol["underlying"].getString();
			minfo.currency_step = symbol["priceIncrement"].getNumber();
			minfo.currency_symbol = "USD";
			minfo.feeScheme = currency;
			minfo.fees = account.fees;
			minfo.invert_price = false;
			minfo.leverage = account.leverage;
			minfo.min_size = minfo.asset_step;
			minfo.min_volume = 0;
			minfo.wallet_id = "futures";
			minfo.type = symbol["type"].getString();
			minfo.expiration = symbol["expiryDescription"].getString();
			minfo.name = symbol["underlyingDescription"].getString();

			if (minfo.type == "move") {
				newsmap.emplace_back(name.str(), minfo);
				StrViewA srch;
				if (minfo.expiration == "Next Week") srch = "This Week";
				else if (minfo.expiration == "Next Day") srch = "Today";
				Value s = result.find([&](Value c) {
					return c["type"].getString() == "move" && c["expiryDescription"].getString() == srch && c["underlying"] == minfo.asset_symbol;
				});
				if (s.hasValue()) {
					minfo.prev_period = s["name"].getString();
					minfo.this_period = name.str();
					name = String({"SPEC:",minfo.asset_symbol,"-",minfo.expiration});
					minfo.expiration.append(" (rollover)");
					newsmap.emplace_back(name.str(), std::move(minfo));
				}
			} else {
				newsmap.emplace_back(name.str(), std::move(minfo));
			}
		}
	}
	smap.swap(newsmap);
	smap_exp = std::chrono::steady_clock::now() + std::chrono::minutes(20);
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
		"1.5",
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
	auto c = connection.lock();
	c->api_key=keyData["key"].getString();
	c->api_secret=keyData["secret"].getString();
	c->api_subaccount=keyData["subaccount"].getString();

	ws_disconnect();

}

void Interface::updateBalances() {
	if (balances.empty()) {
		Value resp = requestGET("/wallet/balances");
		if (resp["success"].getBool()) {
			resp = resp["result"];
			BalanceMap::Set::VecT bmap;
			for (Value r : resp) {
				String symbol = r["coin"].toString();
				double val = r["total"].getNumber();
				bmap.emplace_back(symbol.str(), val);
			}
			balances.swap(bmap);
		} else {
			throw std::runtime_error("Failed to get balances");
		}
	}
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
			if (iter->second.this_period.empty()) {
				auto iter = acc.positions.find(pair);
				if (iter == acc.positions.end()) return 0.0;
				else return iter->second;
			} else {
				auto iter1 = acc.positions.find(iter->second.this_period);
				auto iter2 = acc.positions.find(iter->second.prev_period);
				double sum1 = iter1 == acc.positions.end()?0.0:iter1->second;
				double sum2 = iter2 == acc.positions.end()?0.0:iter2->second;
				return sum1 + sum2;
			}
		}
	} else {
		updateBalances();
		auto iter = balances.find(symb);
		if (iter == balances.end()) return 0.0;
		else return iter->second;
	}
}


IStockApi::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	std::ostringstream uri;
	std::string symb ( pair);
	if (smap.empty()) updatePairs();
	auto iter = smap.find(symb);
	if (iter == smap.end()) throw std::runtime_error("Unknown symbol");
	Value resp;
	auto readTrades = [&](const std::string_view &pair) {

		uri.str("");uri.clear();
		uri << "/fills?market=" << simpleServer::urlEncode(pair);
		if (lastId.hasValue()) {
			auto start_time = lastId[0].getUIntLong()/1000;
			uri << "&limit=100"
					<< "&start_time=" << start_time;
		} else {
			uri << "&limit=1";
		}
		json::Value resp = requestGET(uri.str());
		return resp;
	};
	if (!iter->second.this_period.empty()) {
		resp = readTrades(iter->second.prev_period);
		if (resp["result"].empty()) {
			resp = readTrades(iter->second.this_period);
		}
	} else {
		resp = readTrades(pair);
	}

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
//	if (!worker.defined()) worker = ondra_shared::Worker::create(1);
}

bool Interface::reset() {
	if (std::chrono::steady_clock::now() > smap_exp) {
		smap.clear();
		updatePairs();
	}
	balances.clear();
	curAccount.reset();

	return true;
}

Value Interface::parseClientId(Value v) {
	if (v.type() == json::string) return Value::fromString(v.getString())[0];
	else return Value();
}

Value Interface::buildClientId(Value v) {
	if (v.defined()) return Value({v, connection.lock()->genOrderNonce()}).stringify();
	else return v;
}

static json::String rawSign(const std::string_view &api_secret, const std::string_view &msg) {
	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);
	HMAC(EVP_sha256(),api_secret.data(), api_secret.length(), reinterpret_cast<const unsigned char *>(msg.data()), msg.length(), digest, &digest_len);
	json::String hexDigest(digest_len*2,[&](char *c){
		const char *hexletters = "0123456789abcdef";
		char *d = c;
		for (unsigned int i = 0; i < digest_len; i++) {
			*d++ = hexletters[digest[i] >> 4];
			*d++ = hexletters[digest[i] & 0xf];
		}
		return d-c;
	});
	return hexDigest;
}



void Interface::ws_disconnect() {
	std::unique_lock _(wslock);
	if (ws != nullptr) {
		ws.close();
		_.unlock();
		ws_reader.join();
		_.lock();
		ws = nullptr;
	}
}

static void ws_post(simpleServer::WebSocketStream ws, json::StrViewA txt) {
	logDebug("WSPOST: $1", txt);
	ws.postText(txt);
}

void Interface::ws_login() {
	std::unique_lock _(wslock);
	simpleServer::WebSocketStream ws = this->ws;
	auto conn = connection.lock();
	_.unlock();
	if (ws!= nullptr) {
		std::ostringstream message;
		auto now = conn->now();
		message << now << "websocket_login";
		json::Value loginStr =
				json::Object("op","login")
				("args",json::Object
					("key", conn->api_key)
					("time", now)
					("subaccount",conn->api_subaccount.empty()?json::Value():json::Value(conn->api_subaccount))
					("sign", rawSign(conn->api_secret, message.str()))
				);
		ws_post(ws, loginStr.toString());
		json::Value subscribe = json::Object("op","subscribe")("channel","orders");
		ws_post(ws, subscribe.toString());
	}
}


void Interface::ws_onMessage(json::StrViewA text) {
	std::unique_lock _(wslock);
	try {
		logDebug("WSRECV $1", text);
		json::Value jmsg = json::Value::fromString(text);
		if (jmsg["channel"].getString() == "orders" && jmsg["type"].getString() == "update") {
			auto data = jmsg["data"];
			auto id = data["id"].getIntLong();
			auto status = data["status"];
			if (status.getString() == "closed") {
				activeOrderMap.erase(id);
				auto iter = replaceOrderMap.find(id);
				if (iter != replaceOrderMap.end()) {
					json::Value newOrder = iter->second;
					replaceOrderMap.erase(iter);
					if (data["filledSize"].getNumber() == 0) {
						Value resp = connection.lock()->requestPOST("/orders", newOrder);
					}
				}
			} else {
				activeOrderMap[id] = data;
			}
		}
	} catch (const std::exception &e) {
		logError("$1", e.what());
	}
}

bool Interface::checkWSHealth() {
	using namespace simpleServer;
	std::unique_lock _(wslock);
	if (ws == nullptr) {
		if (ws_reader.joinable()) ws_reader.join();
		activeOrderMap.clear();
		replaceOrderMap.clear();
		auto conn = connection.lock();
		auto &client = conn->api.getClient();
		ws = simpleServer::connectWebSocket(client, "https://ftx.com/ws");
		ws_reader = std::thread([ws = this->ws, this]() mutable {
			try {
				bool cont = true;
				bool tm = false;
				auto nextPing = std::chrono::steady_clock::now();
				while (cont && ws.readFrame()) {
					switch (ws.getFrameType()) {
					default:
					case WSFrameType::binary: break;
					case WSFrameType::connClose: cont = false;break;
					case WSFrameType::ping:break;
					case WSFrameType::text: ws_onMessage(ws.getText());
											break;
					}
					tm = false;
					do {
						auto now = std::chrono::steady_clock::now();
						if (now > nextPing) {
							ws_post(ws, "{\"op\":\"ping\"}");
							nextPing = now + std::chrono::seconds(10);
							if (tm) {
								logError("Closing websocket,  because timeout");
								std::unique_lock _(wslock);
								this->ws = nullptr;
								return;
							}
							tm = true;
						}
					}	while (!ws.getStream().waitForInput(10000));
				}
			} catch (std::exception &e) {
				logError("WS exception: $1", e.what());
			}
			std::unique_lock _(wslock);
			this->ws = nullptr;
		});
		conn.release();
		_.unlock();
		ws_login();
		json::Value resp = requestGET("/orders");
		if (resp["success"].getBool()) {
			_.lock();
			auto res = resp["result"];
			for (Value x: res) {
				activeOrderMap[x["id"].getIntLong()] = x;
			}
		} else {
			throw std::runtime_error(resp.stringify().str());
		}

		return false;
	} else {
		return true;
	}
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {

	checkWSHealth();

	std::string symb ( pair);
	if (smap.empty()) updatePairs();
	auto iter = smap.find(symb);
	if (iter == smap.end()) throw std::runtime_error("Unknown symbol");
	if (!iter->second.this_period.empty()) symb = iter->second.this_period;

	std::unique_lock _(wslock);
	IStockApi::Orders orders;
	for (const auto &item: activeOrderMap) {
		if (item.second["market"].getString() == StrViewA(symb)) {
			Value v = item.second;
			orders.push_back({
				v["id"],
				parseClientId(v["clientId"]),
				(v["side"].getString() == "sell"?-1:1) * v["remainingSize"].getNumber(),
				v["price"].getNumber()
			});
		}
	}
	return orders;

}

std::string numberToFixed(double numb, int fx) {
	std::ostringstream str;
	str.precision(fx);
	str.setf(std::ios_base::fixed);
	str << numb;
	return str.str();
}

json::Value Interface::placeOrderImpl(PConnection conn, const std::string_view &pair, double size, double price, json::Value ordId) {
	return checkCancelAndPlace(conn, pair, size, price, ordId, nullptr, 0);
}

bool Interface::cancelOrderImpl(PConnection conn, json::Value cancelId) {
	std::ostringstream uri;
	if (cancelId.hasValue()) {
		uri << "/orders/" << simpleServer::urlEncode(cancelId.toString());
		Value resp = conn.lock()->requestDELETE(uri.str());
		if (!resp["success"].getBool()) {
			throw std::runtime_error(resp.stringify().str());
		}
	}
	return true;

}

json::Value Interface::checkCancelAndPlace(PConnection conn, std::string_view pair,
		double size, double price, json::Value ordId,
		json::Value replaceId, double replaceSize) {

	checkWSHealth();

	std::unique_lock _(wslock);

	Value req = Object
				("market", pair)
				("side", size > 0?"buy":"sell")
				("price", price)
				("type","limit")
				("size", std::abs(size))
				("postOnly",true)
				("clientId", ordId);

	auto id = replaceId.getUIntLong();
	auto iter = activeOrderMap.find(id);
	if (iter == activeOrderMap.end()) {
		Value resp = conn.lock()->requestPOST("/orders", req);
		if (resp["success"].getBool()) {
			return resp["result"]["id"];
		} else {
			throw std::runtime_error(resp.stringify().str());
		}
	} else {
		replaceOrderMap[id] = req;
		cancelOrderImpl(conn, replaceId);
	}
	return replaceId;
}


json::Value Interface::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {

	std::string symb ( pair);
	if (smap.empty()) updatePairs();
	auto iter = smap.find(symb);
	if (iter == smap.end()) throw std::runtime_error("Unknown symbol");
	if (!iter->second.this_period.empty()) {
		symb = iter->second.this_period;
		if (iter->second.period_checked == false) {
			bool ok = close_position(iter->second.prev_period);
			iter->second.period_checked = ok;
			if (!ok) throw std::runtime_error("Waiting for rollover");
		}
	}

	if (size == 0) {
		if (replaceId.defined()) {
			cancelOrderImpl(connection,replaceId);
		}
		return nullptr;
	} else if (!replaceId.defined()) {
		auto ordId = buildClientId(clientId);
		return placeOrderImpl(connection, symb,size, price, ordId);
	} else {
		auto ordId = buildClientId(clientId);
		return checkCancelAndPlace(connection, symb, size,price,ordId,replaceId,replaceSize);
	}
}

double Interface::getFees(const std::string_view &pair) {
	const AccountInfo &account = getAccountInfo();
	return account.fees;
}


IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {
	if (smap.empty()) updatePairs();
	auto iter = smap.find(pair);
	if (iter == smap.end()) throw std::runtime_error("unknown symbol");
	std::string symb = iter->second.this_period.empty()?std::string(pair):iter->second.this_period;
	std::ostringstream uri;
	uri << "/markets/" << simpleServer::urlEncode(symb) << "/orderbook?depth=1";
	json::Value resp = publicGET(uri.str());
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
	auto c = connection.lock();
	return !c->api_key.empty() && !c->api_secret.empty();
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

json::Value Interface::publicGET(std::string_view path) {
	return connection.lock()->api.GET(path);
}
json::Value Interface::requestGET(std::string_view path) {
	return connection.lock()->requestGET(path);
}
json::Value Interface::Connection::requestGET(std::string_view path) {
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
	return connection.lock()->requestPOST(path, params);
}

json::Value Interface::Connection::requestPOST(std::string_view path, json::Value params) {
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
	return connection.lock()->requestDELETE(path);
}

json::Value Interface::Connection::requestDELETE(std::string_view path, json::Value params) {
	try {
		return api.DELETE(path, params, signHeaders("DELETE",path, params));
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v["error"].toString().str());
		} catch (...) {
			throw;
		}
	}
}


json::Value Interface::Connection::requestDELETE(std::string_view path) {
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

json::Value Interface::Connection::signHeaders(const std::string_view &method, const std::string_view &path, const Value &body) {
	if (api_key.empty() || api_secret.empty()) throw std::runtime_error("Need API key");

	std::ostringstream buff;
	auto ts = now();
	buff << ts << method << "/api" << path;
	if (body.defined()) body.toStream(buff);
	std::string msg = buff.str();

	auto hexDigest = rawSign(api_secret, msg);
	Value req =  Object
			("FTX-KEY", api_key)
			("FTX-TS", ts)
			("FTX-SIGN", hexDigest)
			("FTX-SUBACCOUNT", api_subaccount.empty()?Value():Value(api_subaccount));

	return req;

}

json::Value Interface::signHeaders(const std::string_view &method,
					    const std::string_view &path,
						const json::Value &body)  {
	return connection.lock()->signHeaders(method, path, body);
}


std::int64_t Interface::Connection::now() {
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

void Interface::Connection::setTime(std::int64_t t ) {
	this->time_diff = 0;
	auto n = now();
	this->time_diff = t - n;
}


int Interface::Connection::genOrderNonce() {
	order_nonce = (order_nonce+1) & 0xFFFFFFF;
	return order_nonce;
}



json::Value Interface::getMarkets() const {
	const_cast<Interface *>(this)->updatePairs();
	Object spot;
	Object futures;
	Object move;
	Object prediction;
	for (const auto &k: smap) {
		if (k.second.type.empty()) {
			auto obj = spot.object(k.second.asset_symbol);
			obj.set(k.second.currency_symbol, k.first);
		} else if (k.second.type == "move") {
			auto obj = move.object(k.second.name);
			obj.set(k.second.expiration, k.first);
		} else if (k.second.type == "prediction") {
			auto obj = prediction.object(k.second.expiration);
			obj.set(k.first, k.first);
		} else {
			auto obj = futures.object(k.second.name);
			obj.set(k.second.expiration, k.first);
		}
	}
	return Object("Spot", spot)("Futures",futures)("Move",move)("Prediction",prediction);
}

bool Interface::close_position(const std::string_view &pair) {
	const AccountInfo &acc = getAccountInfo();
	if (!getOpenOrders(pair).empty()) {
		connection.lock()->requestDELETE("/orders", Object("market", pair));
	}
	auto iter = acc.positions.find(pair);
	if (iter != acc.positions.end()) {
		double size = iter->second;
		if (size) {
			double mark_price = getMarkPrice(pair);
			Object req;
			req	("market", pair)
				("side", size>0?"sell":"buy")
				("price",mark_price)
				("type","limit")
				("size",std::fabs(size))
				("reduceOnly",true);
			connection.lock()->requestPOST("/orders", req);
			return false;
		} else {
			return true;
		}
	}
	return true;
}

json::Value Interface::getWallet_direct() {
	updateBalances();
	Object res;
	for (auto &&x: balances) {
		if (x.second)
			res.set(x.first, x.second);
	}
	Object pos;
	const AccountInfo &acc = getAccountInfo();
	for (auto &&x: acc.positions) {
		if (x.second)
			pos.set(x.first, x.second);
	}


	Object out;
	out.set("spot", res);
	out.set("positions", pos);
	out.set("collateral", Object("USD", acc.colateral));
	return out;
}

double Interface::getMarkPrice(const std::string_view &pair) {
	std::string uri = "/futures/"+simpleServer::urlDecode(pair);
	Value resp = publicGET(uri);
	if (resp["success"].getBool()) {
		double x = resp["result"]["mark"].getNumber();
		if (x) return x;
		throw std::runtime_error("Can't get mark price (zero price)");
	} else {
		throw std::runtime_error("Can't get mark price (success=false)");
	}
}

json::Value Interface::testCall(const std::string_view &method, json::Value args) {
	if (method == "getMarkPrice") {
		return getMarkPrice(args.getString());
	} else 	if (method == "closePosition") {
		return close_position(args.getString());

	} else {
		return AbstractBrokerAPI::testCall(method, args);
	}
}
