/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/parser.h>
#include <imtjson/binary.h>
#include <openssl/hmac.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stack>

#include "../api.h"
#include "../httpjson.h"
#include "../isotime.h"
#include "../orderdatadb.h"
#include <imtjson/binjson.tcc>
#include <imtjson/operations.h>
#include <shared/logOutput.h>

using ondra_shared::logDebug;



using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	HTTPJson httpc;
	OrderDataDB orderdb;

	Interface(const std::string &path)
		:AbstractBrokerAPI(path,{
			Object({
				{"name","passphrase"},
				{"label","Passphrase"},
				{"type", "string"}
			}),
			Object({
				{"name","privKey"},
				{"label","Secret"},
				{"type", "string"}
			}),
			Object({
				{"name","pubKey"},
				{"label","KeyID"},
				{"type", "string"}
			}),
			Object({
				{"name","site"},
				{"label","Site"},
				{"type", "enum"},
				{"options", Object({
					{"live","Live"},
					{"sandbox","Sandbox (test)"}})}
			})
		})
	,httpc(simpleServer::HttpClient("MMBot 2.0 coinbase API client", simpleServer::newHttpsProvider(), nullptr, nullptr),"https://api.pro.coinbase.com")
	,orderdb(path+".db")

	{
		nonce = now();
	}


	virtual double getBalance(const std::string_view & symb) override {return 0;}
	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId,
			json::Value replaceId,
			double replaceSize) override;
	virtual bool reset() override ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override ;
	virtual double getFees(const std::string_view &pair) override ;
	virtual std::vector<std::string> getAllPairs() override ;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override {}
	virtual Interface *createSubaccount(const std::string &path) override {
		return new Interface(path);
	}
	virtual json::Value getWallet_direct() override;

	static std::uint64_t now() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
	}

	bool canTrade() const;
	void checkCanTrade() const;

	std::string publicKey, privateKey, passphrase;
	std::uint64_t nonce;

	Value ratesCache;;

	Value getAllPairsJSON();


	Value createHeaders(std::string_view method, std::string_view path, Value body);
	Value readTradePage(unsigned int pageId, const std::string_view &pair);
	Value balanceCache;
	Value orderCache;
	Value feesCache;
	bool simulator = true;
};

std::int64_t timeOffset = 0;
std::uint64_t timeOffsetValidity = 0;


static void handleError() {
	try {
		throw;
	} catch (const HTTPJson::UnknownStatusException &exp) {
		json::Value resp;
		try {resp = json::Value::parse(exp.response.getBody());} catch (...) {}
		if (resp.hasValue()) {
			throw std::runtime_error(resp["message"].getString());
		} else {
			throw;
		}
	}
}

std::string toUpperCase(std::string_view z) {
	std::string r;
	r.reserve(z.size());
	std::transform(z.begin(),z.end(),std::back_inserter(r), toupper);
	return r;
}

std::string toLowerCase(std::string_view z) {
	std::string r;
	r.reserve(z.size());
	std::transform(z.begin(),z.end(),std::back_inserter(r), tolower);
	return r;
}

Value Interface::createHeaders(std::string_view method, std::string_view path, Value body) {

	checkCanTrade();
	std::uint64_t tm = now()/1000+timeOffset;
	std::ostringstream buff;
	buff << tm << method << path;
	if (body.hasValue()) body.toStream(buff);
	std::string message = buff.str();

	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);

	HMAC(EVP_sha256(), privateKey.data(), privateKey.length(), reinterpret_cast<const unsigned char *>(message.data()), message.length(),digest,&digest_len);
	/*
	std::string sign;
	base64->encodeBinaryValue(BinaryView(digest, digest_len), [&](StrViewA txt) {
		sign.append(txt.data, txt.length);
	});
*/
	Value v =  Object({
			{"CB-ACCESS-KEY", publicKey},
			{"CB-ACCESS-SIGN", Value(json::BinaryView(digest, digest_len),base64)},
			{"CB-ACCESS-TIMESTAMP",tm},
			{"CB-ACCESS-PASSPHRASE",passphrase}});
	return v;

}


double Interface::getBalance(const std::string_view& symb, const std::string_view & pair) {
	try {
		if (!balanceCache.hasValue()) {
			std::string path = "/accounts";
			balanceCache = httpc.GET(path,createHeaders("GET",path,Value()));
		}
		Value f = balanceCache.find([&](Value v) {
			return v["currency"].getString() == symb;
		});
		return f["balance"].getNumber();
	} catch (...) {handleError();throw;}
}


inline Interface::TradesSync Interface::syncTrades(json::Value lastId,const std::string_view& pair) {
	try {
		std::string path = "/fills?product_id=";
		path.append(pair);
		std::size_t plen = path.length();
		if (lastId.hasValue()) {
			std::stack<Trade> td;
			Value firstId;
			bool rep = true;
			do {
				Value resp = httpc.GET(path,createHeaders("GET",path,Value()));
				if (resp.empty()) break;
				if (!firstId.hasValue()) firstId = resp[0]["trade_id"];
				Value id;
				for (Value x: resp) {
					id = x["trade_id"];
					if (id == lastId) {
						rep = false;break;
					}
					double price = x["price"].getNumber();
					double size = x["size"].getNumber();
					if (x["side"].getString() == "sell") size = -size;
					double eff_price = (size*price + x["fee"].getNumber())/size;
					td.push(Trade{x["trade_id"],
						parseTime(x["created_at"].toString(),ParseTimeFormat::iso),
						size,
						price,
						size,
						eff_price
					});
				}
				path.resize(plen);
				path.append("&after=");
				path.append(id.toString().str());

			} while (rep);
			TradesSync sync;
			sync.lastId = firstId;
			while (!td.empty()) {
				sync.trades.push_back(td.top());
				td.pop();
			}
			if (!sync.trades.empty()) feesCache = json::undefined;
			return sync;

		} else {
			path.append("&limit=1");
			Value resp = httpc.GET(path,createHeaders("GET",path,Value()));
			if (!resp.empty()) {return {
				TradeHistory{}, resp[0]["trade_id"]
			};} else {
				return {};
			}
		}

		return {};
	} catch (...) {handleError();throw;}
}
/*
static Value decodeClientID(const Value coid) {
	if (coid.type() != json::string) return json::undefined;
	json:: Binary bin = coid.getBinary(base64url);
	auto iter = bin.begin();
	json::Value parsed = json::Value::parseBinary([&]{
		return *iter++;
	});
	return parsed[0];
}

static Value encodeClientID(const Value coid, std::uint64_t nonce) {
	if (!coid.defined()) return json::undefined;
	std::vector<unsigned char> data;
	(Value({coid,nonce})).serializeBinary([&](int c) {data.push_back(static_cast<unsigned char>(c));});
	return Value(json::BinaryView(data.data(), data.size()),json::base64url);
}
*/
inline Interface::Orders Interface::getOpenOrders(const std::string_view& pair) {
	try {
		if (!orderCache.hasValue()) {
			std::string path="/orders";
			orderCache = httpc.GET(path,createHeaders("GET",path,Value()));
			for(Value v: orderCache) {
				orderdb.mark(v["id"]);
			}
		}
		Value o = orderCache.filter([&](Value o){
			return o["product_id"].getString() == pair;
		});

		return mapJSON(o,[&](Value ord){
			return Order {
				ord["id"],
				orderdb.get(ord["id"]),
				ord["size"].getNumber()*(ord["side"].getString() == "sell"?-1:1),
				ord["price"].getNumber()
			};
		},Orders());
	} catch (...) {handleError();throw;}
}

inline Interface::Ticker Interface::getTicker(const std::string_view& pair) {
	try {
		std::ostringstream buff;
		buff << "/products/" << pair << "/ticker";
		Value v = httpc.GET(buff.str());
		return Ticker {
			v["bid"].getNumber(),
			v["ask"].getNumber(),
			v["price"].getNumber(),
			parseTime(v["time"].toString(), ParseTimeFormat::iso)
		};
	} catch (...) {handleError();throw;}

}

inline json::Value Interface::placeOrder(const std::string_view& pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {
	try {
		if (replaceId.hasValue()) {
			std::string path = "/orders/"; path.append(replaceId.getString());
			httpc.DELETE(path,Value(),createHeaders("DELETE",path,Value()));
		}
		if (size) {
			std::string path = "/orders";
			Value req = Object({
				{"type","limit"},
				{"side", size < 0?"sell":"buy"},
				{"product_id", pair},
				{"price", price},
				{"size", std::abs(size)},
				{"post_only", true}});
			Value resp = httpc.POST(path, req, createHeaders("POST", path, req));
			Value id = resp["id"];
			orderdb.store(id, clientId);
			return id;
		}

		return nullptr;
	} catch (...) {
		handleError();throw;
	}



}

inline bool Interface::reset() {
	balanceCache = json::undefined;
	orderCache = json::undefined;
	auto n = now();
	if (n > timeOffsetValidity) {
		Value tm = httpc.GET("/time");
		auto svtm = tm["epoch"].getIntLong();
		auto mytm = now();
		timeOffset = svtm - mytm/1000;
		timeOffsetValidity = mytm+300*1000;
		logDebug("Time offset: $1", timeOffset);
	}
	return true;
}


inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view& pair) {
	std::ostringstream buff;
	buff << "/products/" << pair;;
	Value v = httpc.GET(buff.str());

	return MarketInfo {
		v["base_currency"].getString(),
		v["quote_currency"].getString(),
		v["base_increment"].getNumber(),
		v["quote_increment"].getNumber(),
		v["base_min_size"].getNumber(),
		0,
		getFees(pair),
		currency,0,false,"",simulator
	};
}

inline double Interface::getFees(const std::string_view& pair) {
	if (canTrade()) {
		if (!feesCache.hasValue()) {
			feesCache = httpc.GET("/fees",createHeaders("GET","/fees",Value()));
		}
		return feesCache["maker_fee_rate"].getNumber();
	} else {
		return 0.005;
	}
}

inline std::vector<std::string> Interface::getAllPairs() {
	try {
		Value v = httpc.GET("/products");
		auto res = mapJSON(v, [&](Value x) {
			return x["id"].toString().str();
		},std::vector<std::string>());
		std::sort(res.begin(), res.end());
		return res;
	} catch (...) {
		handleError();throw;
	}
}

bool Interface::canTrade() const {
	return !this->privateKey.empty() && !this->publicKey.empty();
}

void Interface::checkCanTrade() const {
	if (!canTrade()) throw std::runtime_error("Operation requires valid API key");
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		canTrade(),
		"coinbase_pro",
		"Coinbase Pro",
		"https://www.coinbase.com/join/novk_3k",
		"1.0",
		R"mit(Copyright (c) 2020 Ondřej Novák

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
"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"
"u6IAAAAASUVORK5CYII=",
false,true
	};
}


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required one argument" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	publicKey = keyData["pubKey"].getString();
	privateKey = map_bin2str(keyData["privKey"].getBinary(json::base64));
	passphrase = keyData["passphrase"].getString();

	auto site = keyData["site"].getString();
	if (site == "live") {
		simulator = false;
		httpc.setBaseUrl("https://api.pro.coinbase.com");
	} else {
		simulator = true;
		httpc.setBaseUrl("https://api-public.sandbox.pro.coinbase.com");
	}
}

json::Value Interface::getWallet_direct() {
	getBalance("","");
	Value spot = balanceCache.map([](Value x){
		double n = x["balance"].getNumber();
		if (n)
			return Value(x["currency"].getString(), n);
		else
			return Value();
	}, json::object);
	return Value(json::object,{Value("exchange", spot)});
}

