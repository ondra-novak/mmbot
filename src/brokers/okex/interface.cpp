/*
 * interface.cpp
 *
 *  Created on: 25. 5. 2021
 *      Author: ondra
 */


#include <imtjson/string.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
#include <imtjson/value.h>
#include <imtjson/operations.h>
#include "interface.h"

#include <openssl/hmac.h>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "../isotime.h"
#include <shared/logOutput.h>
#include "../../imtjson/src/imtjson/binary.h"
#include "../../server/src/simpleServer/urlencode.h"


namespace okex {

using namespace json;
using ondra_shared::logDebug;
using ondra_shared::logError;

Interface::Interface(const std::string &config_path)
:AbstractBrokerAPI(config_path,{
		Object({
			{"name","passphrase"},
			{"label","Passphrase"},
			{"type","string"}
		}),
		Object({
			{"name","key"},
			{"label","API Key"},
			{"type","string"}
		}),
		Object({
			{"name","secret"},
			{"label","Secret Key"},
			{"type","string"}
		})
})
,api(simpleServer::HttpClient(
		"Mozilla/5.0 (compatible; MMBOT/2.0; +http://github.com/ondra-novak/mmbot.git)",
		simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(60)),
		"https://www.okex.com")
{

}

AbstractBrokerAPI* Interface::createSubaccount(
		const std::string &secure_storage_path) {
	return new Interface(secure_storage_path);
}

const Interface::MarketInfo &Interface::getMkInfo(const std::string_view &pair) {
	auto iter = mkcache.find(pair);
	if (iter == mkcache.end()) {
		getMarketInfo(pair);
		iter = mkcache.find(pair);
		if (iter == mkcache.end()) throw std::runtime_error("Unknown instrument");
	}
	return iter->second;

}

IStockApi::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	updateInstruments();
	Value f = instr_cache.find([&](Value x){
		return x["instId"].getString() == pair;
	});
	if (f.defined()) {
		MarketInfo nfo{
			/*asset_symbol=*/ f["baseCcy"].getString(),
			/*currency_symbol=*/ f["quoteCcy"].getString(),
			/*asset_step=*/ f["lotSz"].getNumber(),
			/*currency_step=*/ f["tickSz"].getNumber(),
			/*min_size=*/ f["minSz"].getNumber(),
			/*min_volume=*/ 0,
			/*fees=*/getFees(pair),
			/*feeScheme = */FeeScheme::income,
			/*leverage = */0,
			/*invert_price =*/ false,
			/*inverted_symbol=*/ std::string(),
			/*simulator = */false,
			/*private_chart = */false,
			/*wallet_id= */ "spot"
		};
		mkcache[std::string(pair)] = nfo;
		return nfo;
	} else {
		throw std::runtime_error("Invalid pair");
	}
}

void Interface::onLoadApiKey(json::Value keyData) {
	api_passphrase = keyData["passphrase"].getString();
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();
}

IBrokerControl::BrokerInfo Interface::getBrokerInfo() {
	return {
		/*trading_enabled = */ !api_key.empty() && !api_passphrase.empty() && !api_secret.empty(),
		/*name=*/ "okex",
		/*exchangeName=*/ "OKEx",
		/*exchangeUrl=*/ "https://www.okex.com/join/8655381",
		/*version=*/ "1.0",
		/*licence=*/ "MIT",
		/*favicon=*/ "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"
		"giBOxz9qkb3VW/9a3gAAAABJRU5ErkJggg=="
,
		/*settings=*/ false,
		/*subaccounts=*/ true

	};
}

void Interface::updateInstruments() const {
	if (!instr_cache.defined())
		instr_cache = api.GET("/api/v5/public/instruments?instType=SPOT")["data"];
}

json::Value Interface::getMarkets() const {
	updateInstruments();
	Value items = instr_cache.sort([](Value a, Value b){
		auto x = Value::compare(a["baseCcy"],b["baseCcy"]);
		if (x) return x;
		else return Value::compare(a["quoteCcy"],b["quoteCcy"]);
	});
	Object res, sub;
	Value cur_bc;
	for (Value it: items) {
		Value bc = it["baseCcy"];
		Value qc = it["quoteCcy"];
		Value id = it["instId"];
		if (bc != cur_bc) {
			if (cur_bc.defined()) res.set(cur_bc.getString(), sub);
			cur_bc = bc;
			sub.clear();
		}
		sub.set(qc.getString(), id);
	}
	if (cur_bc.defined()) res.set(cur_bc.getString(), sub);
	return Value(json::object,{Value("Spot",res)});
}

bool Interface::reset() {
	instr_cache=Value();
	account_cache=Value();
	return true;
}

std::vector<std::string> Interface::getAllPairs() {
	updateInstruments();
	return mapJSON(instr_cache, [](Value x){
		return std::string(x["instId"].getString());
	}, std::vector<std::string>());

}

IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {
	std::string uri="/api/v5/market/ticker?instId=01234567890123456789";
	uri.resize(uri.length()-20);uri.append(pair);

	Value res = api.GET(uri)["data"][0];
	return {
		/*bid=*/ res["bidPx"].getNumber(),
		///The first ask
		/*ask=*/ res["askPx"].getNumber(),
		///Last price
		/*last=*/ res["last"].getNumber(),
		///Time when read
		/*time=*/ res["ts"].getUIntLong()
	};
}

json::Value Interface::testCall(const std::string_view &method,
		json::Value args) {
	return nullptr;
}

void Interface::updateAccountData() {
	if (!account_cache.defined())
		account_cache = authReq("GET", "/api/v5/account/account-position-risk", Value());
}

double Interface::getBalance(const std::string_view &symb,
		const std::string_view &pair) {
	updateAccountData();
	Value entry = account_cache["data"][0]["balData"].find([&](Value v){
		return v["ccy"].getString() == symb;
	});
	return entry["eq"].getNumber();
}

IStockApi::TradesSync Interface::syncTrades(json::Value lastId,
		const std::string_view &pair) {
	std::ostringstream buff;
	buff << "/api/v5/trade/fills?instId=" << simpleServer::urlDecode(pair);
	if (!lastId.hasValue()) {
		buff << "&limit=1";
		Value res = authReq("GET", buff.str(), Value())["data"];
		if (res.empty()) {
			return TradesSync{{}, ""};
		} else {
			return TradesSync{{}, res[0]["billId"]};
		}
	} else {
		if (!lastId.getString().empty()) buff << "&before=" << lastId.getString();
		Value res = authReq("GET", buff.str(), Value())["data"]
						.sort([](Value a, Value b){
			return a["ts"].getNumber()-b["ts"].getNumber();
		});
		if (res.empty()) {
			return TradesSync{{}, lastId};
		} else {
			Value new_lastId = res[res.size()-1]["billId"];
			const MarketInfo &mk = getMkInfo(pair);
			return TradesSync{
				mapJSON(res, [&](Value z){
					double size = z["fillSz"].getNumber();
					double price = z["fillPx"].getNumber();
					if (z["side"].getString() == "sell") size= -size;
					auto ts = z["ts"].getUIntLong();
					String feeCcy = z["feeCcy"].toString();
					double fee = z["fee"].getNumber();
					double eff_size = size;
					double eff_price = price;
					if (fee!=0) {
						if (feeCcy.str() == StrViewA(mk.asset_symbol)) {
							eff_size+=fee;
						} else if (feeCcy.str() == StrViewA(mk.currency_symbol)) {
							if (size) {
								eff_price=(size*price+fee)/size;
							}
						}
					}
					return Trade{
						z["tradeId"],
						ts,
						size,
						price,
						eff_size,
						eff_price
					};
				}, TradeHistory()), new_lastId
			};
		}


	}


	return {};
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {
	std::string uri="/api/v5/trade/orders-pending?ordType=post_only&instId=01234567890123456789";
	uri.resize(uri.length()-20);uri.append(simpleServer::urlEncode(pair));

	Value res = authReq("GET", uri, Value())["data"];
	return mapJSON(res, [&](Value x){
		return Order{
			x["ordId"],
			parseTag(x["tag"].getString()),
			(x["sz"].getNumber()-x["accFillSz"].getNumber())*(x["side"].getString()=="sell"?-1.0:1.0),
			x["px"].getNumber()
		};
	}, Orders());
}

json::Value Interface::placeOrder(const std::string_view &pair, double size,
		double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {
		if (size) {
			Value res = authReq("POST", "/api/v5/trade/amend-order", Object({
				{"instId",pair},
				{"ordId",replaceId},
				{"newSz", std::abs(size)},
				{"newPx", price}
			}));
			return res["data"][0]["ordId"];
		} else {
			Value res = authReq("POST","/api/v5/trade/cancel-order", Object({
				{"instId",pair},
				{"ordId", replaceId}
			})
			);
			return nullptr;
		}
	} else if (size) {

		Value res = authReq("POST", "/api/v5/trade/order", Object({
			{"instId", pair},
			{"tdMode","cash"},
			{"tag", createTag(clientId)},
			{"side",size<0?"sell":"buy"},
			{"ordType","post_only"},
			{"sz", std::abs(size)},
			{"px",price}
		})
				);
		return res["data"][0]["ordId"];
	} else {
		return nullptr;
	}


}

double Interface::getFees(const std::string_view &pair) {
	if (api_key.empty() || api_secret.empty() || api_passphrase.empty()) return 0.001;
	else {
		std::string uri="/api/v5/account/trade-fee?instType=SPOT&instId=01234567890123456789";
		uri.resize(uri.length()-20);uri.append(simpleServer::urlEncode(pair));
		Value res = authReq("GET", uri, Value());
		return -res["data"][0]["maker"].getNumber();
	}
}

IBrokerControl::AllWallets Interface::getWallet() {
	updateAccountData();

	return AllWallets{
		Wallet{"spot",mapJSON(account_cache["data"][0]["balData"],[](Value c){
			return WalletItem {
				c["ccy"].toString(),
				c["eq"].getNumber()
			};
		},std::vector<WalletItem>())
		}
	};
}


void Interface::onInit() {}


static std::string timeToISO(const std::chrono::system_clock::time_point &tp) {

	time_t raw_time = std::chrono::system_clock::to_time_t(tp);
	struct tm  timeinfo;
	gmtime_r(&raw_time, &timeinfo);

	std::ostringstream ss;
	ss << std::put_time(&timeinfo, "%FT%T.");
 	std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
 	ss << std::setw(3) << std::setfill('0') << (ms.count() % 1000) << "Z";
	return ss.str();
}

json::Value Interface::authReq(std::string_view method, std::string_view uri, json::Value body) const {
	auto now = api.now();
	std::string nowstr = timeToISO(now);
	String bodystr = body.defined()?body.stringify():"";
	String prehash { nowstr, method, uri, bodystr};
	unsigned char digest[100];
	unsigned int digest_len;
	HMAC(EVP_sha256(), api_secret.data(), api_secret.length(), reinterpret_cast<const unsigned char *>(prehash.c_str()), prehash.length(), digest, &digest_len);
	Value hdrs (json::object,{
			Value("OK-ACCESS-KEY",api_key),
			Value("OK-ACCESS-SIGN",Value(json::BinaryView(digest,digest_len),json::base64)),
			Value("OK-ACCESS-TIMESTAMP",nowstr),
			Value("OK-ACCESS-PASSPHRASE",api_passphrase)
	});

	try {
		if (method=="GET") return api.GET(uri,std::move(hdrs));
		Value v = api.SEND(uri, method, body, std::move(hdrs));
		if (v["code"].getUInt() != 0) {
			Value m1 = v["msg"];
			Value m2 = v["data"][0]["sMsg"];
			std::ostringstream buff;
			buff<<v["code"].toString()<<" : "<<(m1.defined()?m1.toString():String())
					<<" : "<<(m2.defined()?m2.toString():String());
			throw std::runtime_error(buff.str());
		}
		return v;
	} catch (const HTTPJson::UnknownStatusException &excp) {
		auto s = excp.response.getBody();
		std::ostringstream sbuff;
		auto c = s.read();
		while (!c.empty()) {
			sbuff.write(reinterpret_cast<const char *>(c.data), c.length);
			c = s.read();
		}
		logError("HTTP-error: $1 $2", excp.code, sbuff.str());
		Value out;
		try {
			json::Value v = Value::fromString(sbuff.str());
			out = v["error_message"];
			if (!out.defined()) out = v;
		} catch (...) {
			out = sbuff.str();
		}
		String zs {std::to_string(excp.code)," ",excp.message," ",out.toString()};
		throw std::runtime_error(zs.str());
	}

}

std::string Interface::createTag(const Value &clientId) {
	if (!clientId.defined()) return "mmbot";
	std::hash<json::Value> h;
	auto hash = h(clientId) & 0xFFFFFFFF;
	std::string buff="mm";
	base64url->encodeBinaryValue(json::BinaryView(reinterpret_cast<const unsigned char *>(&hash),4),
			[&](StrViewA c){buff.append(c.data, c.length);});
	clientIdMap[buff] = clientId;
	return buff;
}

json::Value Interface::parseTag(const std::string_view &tag) {
	auto iter = clientIdMap.find(tag);
	if (iter == clientIdMap.end()) return json::Value();
	else return iter->second;
}

}

