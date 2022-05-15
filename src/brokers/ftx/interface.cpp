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
#include <cstring>
#include <future>

#include <imtjson/object.h>
#include <imtjson/binary.h>
#include "../isotime.h"
#include <imtjson/operations.h>
#include <imtjson/parser.h>
#include <imtjson/string.h>
#include <imtjson/value.h>
#include <shared/logOutput.h>
#include <shared/stringview.h>
#include "../../userver/websockets_client.h"
#include "shared/worker.h"
using json::Object;
using json::String;
using json::Value;
using ondra_shared::logDebug;
using ondra_shared::logError;

//static ondra_shared::Worker worker;




Interface::Connection::Connection()
	:api("https://ftx.com/api") {
	auto nonce = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*10;
	order_nonce = nonce & 0xFFFFFFF;

}

Interface::Interface(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path,
			{
						Object({
							{"name","key"},
							{"label","API Key"},
							{"type","string"}}),
						Object({
							{"name","secret"},
							{"label","API Secret"},
							{"type","string"}}),
						Object({
							{"name","subaccount"},
							{"label","Subaccount (optional)"},
							{"type","string"}})
			})
{
	connection=PConnection::make();

}


void Interface::updatePairs() {
	Value mres = publicGET("/markets");
	Value result = mres["result"];
	const AccountInfo &account = getAccountInfo();
	SymbolMap::Set::VecT newsmap;
	std::map<json::String, json::Value> minProvideSize;

	for (Value symbol: result) {
		if (symbol["enabled"].getBool() && symbol["type"] == "spot") {
			MarketInfoEx minfo;
			String name = symbol["name"].toString();

			minfo.asset_step = symbol["sizeIncrement"].getNumber();
			minfo.asset_symbol = symbol["baseCurrency"].getString();
			minfo.currency_step = symbol["priceIncrement"].getNumber();
			minfo.currency_symbol = symbol["quoteCurrency"].getString();
			minfo.feeScheme = FeeScheme::income;
			minfo.fees = account.fees;
			minfo.invert_price = false;
			minfo.leverage = 0;
			minfo.min_size = symbol["minProvideSize"].getNumber();
			minfo.min_volume = 0;
			minfo.wallet_id = "spot";
			newsmap.emplace_back(name.str(), std::move(minfo));
		} else {
			String name = symbol["name"].toString();
			Value minSz = symbol["minProvideSize"];
			minProvideSize[name] = minSz;
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
			minfo.feeScheme = FeeScheme::currency;
			minfo.fees = account.fees;
			minfo.invert_price = false;
			minfo.leverage = account.leverage;
			minfo.min_size = minProvideSize[name].getNumber();
			minfo.min_volume = 0;
			minfo.wallet_id = "futures";
			minfo.type = symbol["type"].getString();
			minfo.expiration = symbol["expiryDescription"].getString();
			minfo.name = symbol["underlyingDescription"].getString();

			if (minfo.type == "move") {
				newsmap.emplace_back(name.str(), minfo);
				std::string_view srch;
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


IBrokerControl::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		hasKey(),
		"ftx",
		"FTX",
		"https://ftx.com/#a=3140432",
		"1.5.1",
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

std::string urlEncode(const std::string_view &text) {
	std::string out;
	json::urlEncoding->encodeBinaryValue(json::map_str2bin(text), [&](std::string_view x){
		out.append(x);
	});
	return out;
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
		uri << "/fills?market=" << urlEncode(pair);
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
		ws->close();
		_.unlock();
		ws_reader.join();
		_.lock();
		ws = nullptr;
	}
}

static void ws_post(std::shared_ptr<userver::WSStream> &ws, std::string_view txt) {
	logDebug("WSPOST: $1", txt);
	ws->send(userver::WSFrameType::text, txt);
}

void Interface::ws_login() {
	std::unique_lock _(wslock);
	auto conn = connection.lock();
	if (ws!= nullptr) {
		std::ostringstream message;
		auto now = conn->now();
		message << now << "websocket_login";
		json::Value loginStr =
				json::Object({
						{"op","login"},
						{"args",json::Object({
							{"key", conn->api_key},
							{"time", now},
							{"subaccount",conn->api_subaccount.empty()?json::Value():json::Value(conn->api_subaccount)},
							{"sign", rawSign(conn->api_secret, message.str())}
						})},

					});
		ws_post(ws, loginStr.toString());
		json::Value subscribe = json::Object({{"op","subscribe"},{"channel","orders"}});
		ws_post(ws, subscribe.toString());
	}
}


void Interface::ws_onMessage(const std::string_view &text) {
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
				auto iter = closeEventMap.find(id);
				if (iter != closeEventMap.end()) {
					auto fn = std::move(iter->second);
					closeEventMap.erase(iter);
					fn(data);
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
	std::unique_lock _(wslock);
	if (ws == nullptr) {
		if (ws_reader.joinable()) ws_reader.join();
		activeOrderMap.clear();
		closeEventMap.clear();
		auto conn = connection.lock();
		auto &client = conn->api.getClient();
		int code;
		ws = std::shared_ptr<userver::WSStream>(userver::wsConnect(client, "https://ftx.com/ws", &code));
		if (ws == nullptr) throw HTTPJson::UnknownStatusException(code, "", json::Value(), json::Value());
		ws_reader = std::thread([ws = this->ws, this]() mutable {
			try {
				bool cont = true;
				auto nextPing = std::chrono::steady_clock::now();
				bool frame = true;
				while (cont) {
					auto t = ws->recvSync();
					switch (t) {
					default:
					case userver::WSFrameType::binary: break;
					case userver::WSFrameType::connClose: cont = false;break;
					case userver::WSFrameType::ping:frame = true;break;
					case userver::WSFrameType::pong:frame = true;break;
					case userver::WSFrameType::text: ws_onMessage(ws->getData()); frame = true; break;
					case userver::WSFrameType::incomplete:
						if (!ws->timeouted() || !frame) {
							logError("Closing websocket,  because error/timeout");
							std::unique_lock _(wslock);
							this->ws = nullptr;
							return;
						}
						ws->clearTimeout();
						frame = false;
						break;
					}

					auto now = std::chrono::steady_clock::now();
					if (now > nextPing) {
						ws_post(ws, "{\"op\":\"ping\"}");
						nextPing = now + std::chrono::seconds(10);
					}

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
		if (item.second["market"].getString() == symb) {
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
		uri << "/orders/" << urlEncode(cancelId.toString());
		try {
			Value resp = conn.lock()->requestDELETE(uri.str());
			if (!resp["success"].getBool()) {
				throw std::runtime_error(resp.stringify().str());
			}
			return true;
		} catch (const std::exception &e) {
			if (std::strstr(e.what(),"Order already closed") != 0) {
				std::unique_lock _(wslock);
				auto id = cancelId.getIntLong();
				activeOrderMap.erase(id);
				closeEventMap.erase(id);
				return false;
			}
			throw;
		}
	}
	return true;

}

json::Value Interface::checkCancelAndPlace(PConnection conn, std::string_view pair,
		double size, double price, json::Value ordId,
		json::Value replaceId, double replaceSize) {

	checkWSHealth();

	std::unique_lock _(wslock);


	if (replaceId.hasValue()) {
		auto id = replaceId.getUIntLong();
		auto iter = activeOrderMap.find(id);
		if (iter == activeOrderMap.end()) {
			return nullptr;
		} else {
			auto promise= std::make_shared<std::promise<json::Value> >();
			auto future = promise->get_future();
			closeEventMap.emplace(id, [promise](json::Value v){
				promise->set_value(std::move(v));
			});
			_.unlock();
			if (cancelOrderImpl(conn, replaceId)) {
				if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
					throw std::runtime_error("Timeout while waiting for close the order");
				}
				auto data = future.get();
				//can't use remainSize - because it is valid only for open order - (for closed, it is always 0)
				double remain = data["size"].getNumber()-data["filledSize"].getNumber();
				if (remain < replaceSize*0.99) {
					return nullptr;
				}
			}
		}
	}

	if (price < 0.000001) {
		throw std::runtime_error("Price is too low < 0.000001");
	}


	Value req = Object({
		{"market", pair},
		{"side", size > 0?"buy":"sell"},
		{"price", price},
		{"type","limit"},
		{"size", std::abs(size)},
		{"postOnly",true},
		{"clientId", ordId}
	});

	Value resp = conn.lock()->requestPOST("/orders", req);
	if (resp["success"].getBool()) {
		return resp["result"]["id"];
	} else {
		throw std::runtime_error(resp.stringify().str());
	}
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
	uri << "/markets/" << urlEncode(symb) << "/orderbook?depth=1";
	json::Value resp = publicGET(uri.str());
	if (resp["success"].getBool()) {
		auto result = resp["result"];
		IStockApi::Ticker tkr;
		tkr.ask = result["asks"][0][0].getNumber();
		tkr.bid = result["bids"][0][0].getNumber();
		tkr.last = (tkr.ask + tkr.bid)*0.5;
		tkr.time = std::chrono::duration_cast<std::chrono::milliseconds>(connection.lock_shared()->api.now().time_since_epoch()).count();
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
			nfo.fees = 0.0002;
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
		if (e.body.defined()) {
			throw std::runtime_error(e.body["error"].toString().str());
		} else {
			throw;
		}
	}
}

json::Value Interface::requestPOST(std::string_view path, json::Value params) {
	return connection.lock()->requestPOST(path, params);
}

json::Value Interface::Connection::requestPOST(std::string_view path, json::Value params) {
	try {
		return api.POST(path, params, signHeaders("POST",path, params));
	} catch (HTTPJson::UnknownStatusException &e) {
		if (e.body.defined()) {
			throw std::runtime_error(e.body["error"].toString().str());
		} else {
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
		if (e.body.defined()) {
			throw std::runtime_error(e.body["error"].toString().str());
		} else {
			throw;
		}
	}
}


json::Value Interface::Connection::requestDELETE(std::string_view path) {
	try {
		return api.DELETE(path, Value(), signHeaders("DELETE",path, Value()));
	} catch (HTTPJson::UnknownStatusException &e) {
		if (e.body.defined()) {
			throw std::runtime_error(e.body["error"].toString().str());
		} else {
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
	Value req =  Object({
		{"FTX-KEY", api_key},
		{"FTX-TS", ts},
		{"FTX-SIGN", hexDigest},
		{"FTX-SUBACCOUNT", api_subaccount.empty()?Value():Value(api_subaccount)}
	});

	return req;

}

json::Value Interface::signHeaders(const std::string_view &method,
					    const std::string_view &path,
						const json::Value &body)  {
	return connection.lock()->signHeaders(method, path, body);
}


std::int64_t Interface::Connection::now() {
	auto n =  std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count();
	return n;

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
	return Object({{"Spot", spot},{"Futures",futures},{"Move",move},{"Prediction",prediction}});
}

bool Interface::close_position(const std::string_view &pair) {
	const AccountInfo &acc = getAccountInfo();
	if (!getOpenOrders(pair).empty()) {
		connection.lock()->requestDELETE("/orders", Object({{"market", pair}}));
	}
	auto iter = acc.positions.find(pair);
	if (iter != acc.positions.end()) {
		double size = iter->second;
		if (size) {
			double mark_price = getMarkPrice(pair);
			Object req;
			req.setItems({
				{"market", pair},
				{"side", size>0?"sell":"buy"},
				{"price",mark_price},
				{"type","limit"},
				{"size",std::fabs(size)},
				{"reduceOnly",true}
			});
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
	out.set("collateral", Object({{"USD", acc.colateral}}));
	return out;
}

double Interface::getMarkPrice(const std::string_view &pair) {
	std::string uri = "/futures/"+urlEncode(pair);
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

bool Interface::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	if (smap.empty()) updatePairs();
	auto iter = std::find_if(smap.begin(), smap.end(), [&](const auto &x) {
		return x.second.asset_symbol == asset && x.second.currency_symbol == currency && x.second.type != "move";
	});
	return iter != smap.end();
}

uint64_t Interface::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<double> &data) {
	if (smap.empty()) updatePairs();
	auto iter = smap.find(std::string(hint_pair));
	if (iter == smap.end()) {
		iter = std::find_if(smap.begin(), smap.end(), [&](const auto &x) {
				return x.second.asset_symbol == asset && x.second.currency_symbol == currency && x.second.type != "move";
		});
		if (iter == smap.end())  return 0;
	}
	std::string uri ="/markets/"+urlEncode(iter->first)+"/candles?resolution=300&start_time="+std::to_string(time_from/1000)+"&end_time="+std::to_string(time_to/1000);
	Value hdata = publicGET(uri);

	auto insert_val = [&](double n){
			data.push_back(n);
	};


	std::uint64_t minDate = time_to;

	for (Value row: hdata["result"]) {
		auto date = parseTime(row["startTime"].toString(), ParseTimeFormat::iso_tm);
		if (date >= time_from && date < time_to) {
				double o = row["open"].getNumber();
				double h = row["high"].getNumber();
				double l = row["low"].getNumber();
				double c = row["close"].getNumber();
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
		return minDate;
	} else {
		return 0;
	}
}

