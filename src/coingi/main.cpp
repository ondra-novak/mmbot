/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/parser.h>
#include <openssl/hmac.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stack>

#include <brokers/api.h>
#include "../brokers/httpjson.h"
#include "../brokers/orderdatadb.h"
#include "../shared/logOutput.h"

using ondra_shared::logDebug;


static const std::unordered_map<std::string_view, double> minVolMap = {
		{"btc",0.0001},
		{"usd",1},
		{"czk",10},
		{"eur",1}
};

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	HTTPJson httpc;
	OrderDataDB orderdb;

	Interface(const std::string &path)
		:AbstractBrokerAPI(path,{Object
				("name","pubKey")
				("label","Public key")
				("type", "string"),
			Object
				("name","privKey")
				("label","Private key")
				("type", "string"),
		})
	,httpc(simpleServer::HttpClient("MMBot 2.0 coingi API client", simpleServer::newHttpsProvider(), nullptr, nullptr),"https://api.coingi.com")
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
	virtual Interface *createSubaccount(const std::string &path) {
		return new Interface(path);
	}

	static std::uint64_t now() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
	}

	bool canTrade() const;
	void checkCanTrade() const;

	std::string publicKey, privateKey;
	std::uint64_t nonce;

	Value ratesCache;;

	Value getAllPairsJSON();


	void createSigned(Object &payload);
	Value readTradePage(unsigned int pageId, const std::string_view &pair);
};

static void handleError() {
	try {
		throw;
	} catch (const HTTPJson::UnknownStatusException &exp) {
		json::Value resp;
		try {resp = json::Value::parse(exp.response.getBody());} catch (...) {}
		if (resp.hasValue()) {
			std::ostringstream buff;
			Value e = resp["errors"];
			if (e.hasValue()) {
				for (Value v: e) {
					buff << v["code"].getUIntLong() << " " << v["message"].getString() << std::endl;
				}
			} else {
				e.toStream(buff);
			}
			throw std::runtime_error(buff.str());
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

void Interface::createSigned(Object &payload) {
	auto nonce = this->nonce++;
	std::ostringstream buff;
	buff << nonce << "$" << publicKey;
	std::string message = buff.str();

	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);

	HMAC(EVP_sha256(), privateKey.data(), privateKey.length(), reinterpret_cast<const unsigned char *>(message.data()), message.length(),digest,&digest_len);

	std::ostringstream digestHex;
	for (unsigned int i = 0; i < digest_len; i++) {
		digestHex << std::hex << std::setw(2) << std::setfill('0')
				<< static_cast<unsigned int>(digest[i]);
	}


	payload.set("token", publicKey);
	payload.set("nonce", nonce);
	payload.set("signature", digestHex.str());
}

double Interface::getBalance(const std::string_view& symb, const std::string_view & pair) {
	try {
		Object req;
		createSigned(req);
		req.set("currencies", toLowerCase(symb));
		Value response = httpc.POST("/user/balance",req);
		Value r = response[0];
		return r["available"].getNumber()+r["inOrders"].getNumber();
	} catch (...) {handleError();throw;}
}

inline Value Interface::readTradePage(unsigned int pageId, const std::string_view &pair) {
	try {
		Object req;
		createSigned(req);
		req("pageNumber", pageId)
		   ("pageSize",100)
		   ("currencyPair", pair);

		return httpc.POST("/user/transactions", req);
	} catch (...) {handleError();throw;}
}


inline Interface::TradesSync Interface::syncTrades(json::Value lastId,
		const std::string_view& pair) {

	unsigned int page = 1;
	TradesSync resp;
	Value tr = readTradePage(page++, pair);
	Value trx = tr["transactions"];
	resp.lastId = nullptr;
	if (trx.size()) {
		resp.lastId = trx[0]["timestamp"];
	}
	if (lastId.hasValue()) {
		bool rep = true;
		auto ltmn = lastId.getUIntLong();
		std::stack<Trade> stk;
		while (rep) {
			rep = false;
			for (Value tx: trx ) {
				Value tm = tx["timestamp"];
				auto tmn = tm.getUIntLong();
				if (tmn <= ltmn) {
					rep = false;
					break;
				}
				double base = tx["baseAmount"].getNumber();
				double counter = tx["counterAmount"].getNumber();
				double fee = tx["fee"].getNumber();
				double price = tx["price"].getNumber();
				int type = tx["orderType"].getInt();
				double size;
				double eff_size;
				double eff_price;
				if (type == 1) {//sell
					size = -base;
					eff_size = -base;
					eff_price = counter/base;
				} else {
					size = base;
					eff_size = base-fee;
					eff_price = counter/(base-fee);
				}
				stk.push(Trade{
					tx["id"],tmn,size,price,eff_size,eff_price
				});
				rep = true;
			}
			if (rep) {
				tr = readTradePage(page++, pair);
				trx = tr["transactions"];
			}
		}
		while (!stk.empty()) {
			const Trade &d = stk.top();
			resp.trades.push_back(d);
			stk.pop();
		}
	}
	return resp;
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view& pair) {
	try {
		Object obj;
		createSigned(obj);
		obj("pageNumber",1)
		   ("pageSize",100)
		   ("currencyPair",pair)
		   ("status",0);

		Value rp = httpc.POST("/user/orders",obj)["orders"];
		return mapJSON(rp, [&](Value o){
			Value id = o["id"];
			orderdb.mark(id);
			Value clientId = orderdb.get(id);
			return Order {
				o["id"],clientId,(o["type"].getUInt()?-1:1)*o["baseAmount"].getNumber(),o["price"].getNumber()
			};
		}, Orders());
	} catch (...) {handleError();throw;}
}

inline Interface::Ticker Interface::getTicker(const std::string_view& pair) {
	try {
	std::string url = "/current/order-book/";
	url.append(pair);
	url.append("/1/1/1");

	Value resp = httpc.GET(url);
	double ask = resp["asks"][0]["price"].getNumber();
	double bid = resp["bids"][0]["price"].getNumber();
	return Ticker {
		bid,ask,std::sqrt(ask*bid),now()
	};
	} catch (...) {handleError();throw;}

}

inline json::Value Interface::placeOrder(const std::string_view& pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {
	try {

		if (replaceId.hasValue()) {
			Object obj;createSigned(obj);
			obj("orderId", replaceId);
			Value resp = httpc.POST("/user/cancel-order", obj);
			double left = resp["baseAmount"].getNumber();
			logDebug("left: $1, replaceSize: $2", left, replaceSize);
			if (left*1.0001 < replaceSize) return nullptr;
		}
		if (size) {
			Object obj;createSigned(obj);
			obj("type",size < 0?1:0)
			   ("volume", std::abs(size))
			   ("price", price)
			   ("currencyPair", pair);
			Value resp = httpc.POST("/user/add-order", obj);
			Value new_id = resp["result"];
			orderdb.store(new_id, clientId);
			return new_id;
		}
		return nullptr;
	} catch (...) {
		handleError();throw;
	}



}

inline bool Interface::reset() {
	return true;
}


inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view& pair) {
	auto pairs = getAllPairsJSON();
	auto tp = pairs[pair];
	if (tp.hasValue()) {

		std::string_view curc = tp["counter"]["name"].getString();
		auto iter = minVolMap.find(curc);
		double minVol = 0;
		if (iter != minVolMap.end()) minVol = iter->second;
		else minVol = 1;

		double price_step = std::pow(10,-tp["priceDecimals"].getNumber());
		double asset_step = std::pow(10,-tp["base"]["volumeDecimals"].getNumber());
		return MarketInfo {
			toUpperCase(tp["base"]["name"].getString()),
		    toUpperCase(tp["counter"]["name"].getString()),
			asset_step,
			price_step,
			asset_step,
			minVol,
			0,
			income
		};
	} else {
		throw std::runtime_error("Unknown pair");
	}
	return {};
}

inline double Interface::getFees(const std::string_view& pair) {
	return 0.002;
}

json::Value Interface::getAllPairsJSON() {
	try {
	auto resp = httpc.GET("/current/currency-pairs");
	Object items;
	for (json::Value v: resp) {
		String id({v["base"]["name"].getString(),"-",v["counter"]["name"].getString()});
		items.set(id, v);
	}
	return items;
	} catch (...) {handleError();throw;}


}

inline std::vector<std::string> Interface::getAllPairs() {
	return mapJSON(getAllPairsJSON(),[&](json::Value z){
		return z.getKey();
	},std::vector<std::string>());
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
		"coingi",
		"Coingi",
		"https://www.coingi.com/?r=XTQ3Y8",
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
"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",
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
	privateKey = keyData["privKey"].getString();
}

