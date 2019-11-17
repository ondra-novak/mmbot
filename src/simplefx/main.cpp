/*
 * main.cpp
 *
 *  Created on: 13. 11. 2019
 *      Author: ondra
 */
#include <vector>

#include "../brokers/api.h"
#include <imtjson/value.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>

#include "../shared/stdLogOutput.h"
#include "../shared/first_match.h"
#include "httpjson.h"
#include "market.h"
#include "quotedist.h"
#include "quotestream.h"
#include "tradingengine.h"

using json::Object;
using json::String;
using json::Value;
using ondra_shared::first_match;



static Value keyFormat = {Object
							("name","key")
							("type","string")
							("label","Key"),
						 Object
							("name","secret")
							("type","string")
							("label","Secret"),
						 Object
							("name","account")
							("type","string")
							("label","Account ID")
};

class Interface: public AbstractBrokerAPI {
public:

	simpleServer::HttpClient httpc;
	HTTPJson hjsn;
	HTTPJson hjsn_utils;
	std::unique_ptr<QuoteStream> qstream;
	std::unique_ptr<Market> market;


	double execCommand(const std::string &symbol, double amount);

	PTradingEngine getEngine(const std::string_view &symbol) {
		try {
			if (market == nullptr) {
				PQuoteDistributor qdist = new QuoteDistributor();
				qstream = std::make_unique<QuoteStream>(httpc,"https://web-quotes.simplefx.com/signalr/", qdist->createReceiveFn());
				qdist->connect(qstream->connect());
				market = std::make_unique<Market>(qdist, [this](const std::string &symbol, double amount){
					return this->execCommand(symbol, amount);
				});
			}
			return market->getMarket(symbol);
		} catch (...) {
			market = nullptr;
			throw;
		}
	}


	void login();

	Interface(const std::string &path):AbstractBrokerAPI(path, keyFormat)
	,httpc("+mmbot/2.0 simplefx_broker (https://github.com/ondra-novak/mmbot)",
			simpleServer::newHttpsProvider())
	,hjsn(httpc,"https://rest.simplefx.com")
	,hjsn_utils(httpc,"https://simplefx.com")
	,logProvider(new LogProvider(*this))
	{
		httpc.setIOTimeout(10000);
		httpc.setConnectTimeout(10000);
	}

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


	std::string authKey;
	std::string authSecret;
	std::string authAccount;

	std::string curReality;
	unsigned int curLogin;
	double balance;


	struct SymbolInfo {
		std::string symbol;
		std::string currency_symbol;
		double mult;
		double step;
		double position;
	};

	struct CurrencyInfo {
		double convRate = 0;
	};

	mutable std::unordered_map<std::string, SymbolInfo> smbinfo;
	mutable std::unordered_map<std::string, CurrencyInfo> curinfo;
	std::uint64_t tokenExpire = 0;


	SymbolInfo &getSymbolInfo(const std::string &symbol);
	void updateSymbols();
	bool hasKey() const;
	void updatePositions();
	void updateRate(const SymbolInfo &sinfo, Value order);


	class LogProvider: public ondra_shared::StdLogProviderFactory {
	public:
		using Super = ondra_shared::StdLogProviderFactory;
		LogProvider(Interface &owner):owner(owner) {}
		virtual void writeToLog(const StrViewA &line, const std::time_t &, ondra_shared::LogLevel ) {
			owner.logMessage(std::string(line));
		}
		void lock() {
			Super::lock.lock();
		}
		void unlock() {
			Super::lock.unlock();
		}
	protected:
		Interface &owner;
	};

	ondra_shared::RefCntPtr<LogProvider> logProvider;
	virtual void flushMessages() override {
		std::lock_guard<LogProvider> _(*logProvider);
		AbstractBrokerAPI::flushMessages();
	}
};



int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required storage path" << std::endl;
		return 1;
	}

	try {

		Interface ifc(argv[1]);
		ifc.logProvider->setDefault();
		ifc.dispatch();


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

static Value getData(Value resp) {
	if (resp["code"].getUInt() != 200) throw std::runtime_error(
			String({resp["code"].toString()," ",resp["message"].toString()}).str()
		);

	return resp["data"];
}

inline double Interface::getBalance(const std::string_view &symb) {
	std::string symbol (symb);
	if (smbinfo.empty()) updateSymbols();
	auto iter = curinfo.find(symbol);
	if (iter == curinfo.end()) {

		SymbolInfo &sinfo = getSymbolInfo(symbol);
		return sinfo.position;
	} else {

		double conv = iter->second.convRate;
		return balance*conv;

	}
	return 0;
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId,
		const std::string_view &pair) {

	PTradingEngine eng = getEngine(pair);
	TradingEngine::UID uid = lastId.getUInt();
	TradesSync s;
	s.lastId = eng->readTrades(uid, [&](const Trade &t) {
		s.trades.push_back(t);
	});

	return s;
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {
	PTradingEngine eng = getEngine(pair);

	Orders lst;
	eng->readOrders([&](const Order &o) {
		lst.push_back(o);
	});

	return lst;
}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	PTradingEngine eng = getEngine(pair);

	return eng->getTicker();
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	PTradingEngine eng = getEngine(pair);
	if (replaceId.defined()) {
		eng->cancelOrder(replaceId.getUInt());
	}
	if (size) {
		return eng->placeOrder(price, size, clientId);
	}


	return nullptr;
}

inline bool Interface::reset() {
	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	const SymbolInfo &sinfo = getSymbolInfo(std::string(pair));
	return MarketInfo {
		std::string(pair),
		sinfo.currency_symbol,
		sinfo.step*sinfo.mult,
		0,
		sinfo.step*sinfo.mult,
		0,
		0,
		currency,
		100,
		false,
		"",
		curReality == "DEMO"
	};
}

inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

inline std::vector<std::string> Interface::getAllPairs() {
	updateSymbols();
	std::vector<std::string> res;
	for (auto &&k : smbinfo) {
		res.push_back(k.first);
	}
	std::sort(res.begin(), res.end());
	return res;
}

inline void Interface::enable_debug(bool e) {
	logProvider->setEnabledLogLevel(e?ondra_shared::LogLevel::debug:ondra_shared::LogLevel::error);
}

inline Interface::BrokerInfo Interface::getBrokerInfo() {
	return {
		hasKey(),
		"simplefx",
		"$impleFX",
		"https://app.simplefx.com/",
		"1.0",
		"Copyright (c) 2019 Ondřej Novák\n\n"

"Permission is hereby granted, free of charge, to any person "
"obtaining a copy of this software and associated documentation "
"files (the \"Software\"), to deal in the Software without "
"restriction, including without limitation the rights to use, "
"copy, modify, merge, publish, distribute, sublicense, and/or sell "
"copies of the Software, and to permit persons to whom the "
"Software is furnished to do so, subject to the following "
"conditions: "
"\n\n"
"The above copyright notice and this permission notice shall be "
"included in all copies or substantial portions of the Software. "
"\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES "
"OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT "
"HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, "
"WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING "
"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR "
"OTHER DEALINGS IN THE SOFTWARE.",
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACABAMAAAAxEHz4AAAAG1BMVEUxAAAXHy1XTFayQUbpTU6B"
"hY2mq67GzM76/fq81alBAAAAAXRSTlMAQObYZgAAA8RJREFUaN7tmT9vozAUwA0kaUboETUjqu50"
"GXNSPgCE+wDJgNSxXXoZ06VkTCKl+GOfnwEbGxsbMnThDYCd5x9+f+xnFIRGGWUUUdzoTsDyct94"
"Z3n14RYEcCuvZYNcg4DqNO5BqdEw4IBxsUdoludkJtMi/4JJ5fkZoYecyOcbIRV5TnUL6Mk/BMAL"
"JlIQbYwv9IpJ54o25pjKHjllL3LLDgEwxS0AmSGZ1ZUBbnSgDrCkXZcaAE0SFHI9MwCOugDkXfvH"
"07EJOFLFLwooTtAhAY4SwEeLqAbM6XRmYJUPjUsA7mgAbnVA6hieqM2oBoBLr9QTmALOABMAchYc"
"AMoBBzr5eQMwNwDAxuKDAUiDDKWhjUrAkwgo3t/fd+0oHDngRoZinGO8A8D1FxYBIAIAlX0VgGTM"
"hYQJ498ArcP42glYlKHmgAM+YvzUBESdAPR4oDZQANE4r8CKB0iXCnBF3QAau3MJIIl9Bq98TaFV"
"AooIdUXBIVkwYwDydIQkuLpVBG9ZFqBOwPzSBJAhR1heFwdmTsNYT10HeMFvwZz5gDztHLqSQFUA"
"VOGEHeFVimJO/UIBK4gHLimFEiCvxocqUj4D+HR9wRqxAtBUpnocsIKVeLAFzHC5dEvACwCWsEHB"
"kxUALf7hTygM0yzboyzL4InssQvyRLqqnMmo7NhdzITTvYXl+wHP4/FglG8XZ/O3JTHinQlT5L9b"
"ADz2nNaKiq4OgOJ1k8bPZgAKWWNdKXK9yAbgye9zNRboAEjW99pu7QZwG3zJBcgO4ElO0FmgBfD+"
"RMyCtSWA25B2WqAHuELYGG5rDUDNSXOttT0gbDjBlWJiBXAbZofqNBYA7Z82HPBTa0EXYMJfq0uC"
"bgCLfeJq0tgAYDakod6CTgAbVxuT+v0Ajjo89gDufOVWYgHwxPEp6guQbEh6Axrbis4CDkiemWhs"
"SG0LS6pKZ50FJkBossAEcE0WmAANG+JhgFC/lfQFoGEAkw97ODFBd4ZxUBSExbAeAPBMu4EJIG4I"
"fm+AtJzXnYCtYjVKG8oW3bul+WjgplqD/qBh2zqrCykaVlh4ZfKHlbbY6UgFm+LKvakoTXbl3dMv"
"ST3AVZ1Qkh6AUHlG6gEQxkzMh6xYVxe3Qiu2BkyEIY42nXUAbrUvLovYEuDpjvtbS0AoBU571NQB"
"9J88sRXAa9WTsB9g0jLZM310xeqzTdw2KrYAeAqXbQwfnrEyjRvKPwyfvrEyBrGqTCZGQKgsBRob"
"VICNcrZqbPk3iPS3R8BEpSn/RTLKKKN8t/wHuIduWNu27QwAAAAASUVORK5CYII="
	};
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	authKey = keyData["key"].getString();
	authSecret = keyData["secret"].getString();
	authAccount = keyData["account"].getString();
}

inline double Interface::execCommand(const std::string &symbol, double amount) {
	login();
	SymbolInfo &sinfo = getSymbolInfo(symbol);
	double adjamount = amount / sinfo.mult;
	Value v = getData(hjsn.POST("/api/v3/trading/orders/market",json::Object
			("Reality",curReality)
			("Login", curLogin)
			("Symbol", symbol)
			("Side",adjamount>0?"BUY":"SELL")
			("Volume", fabs(adjamount))
			("IsFIFO", true)));

	Value morder = v["marketOrders"][0];
	Value order = morder["order"];
	updateRate(sinfo, order);
	sinfo.position += amount;
	switch (morder["action"].getUInt()) {
		case 1: {
			return order["openPrice"].getNumber();
		}
		case 3: {
			return order["closePrice"].getNumber();
		}
		default: {
			if (order["closeTime"].hasValue()) {
				return order["closePrice"].getNumber();
			} else {
				return order["openPrice"].getNumber();
			}
		}


	}
}

inline void Interface::login() {
	auto now = TradingEngine::now();
	if (hasKey() && (!hjsn.hasToken() || tokenExpire < now)) {
		Value v = getData(hjsn.POST("/api/v3/auth/key", json::Object
				  ("clientId", authKey)
				  ("clientSecret",authSecret)));
		Value token = v["token"];
		hjsn.setToken(token.getString());
		tokenExpire = now + 15*60000;
		Value accounts = getData(hjsn.GET("/api/v3/accounts"));
		curLogin = 0;
		curReality = "";
		for (Value v: accounts) {
			Value login = v["login"];
			Value reality = v["reality"];
			if (login.toString().str() == StrViewA(authAccount)) {
				curLogin = login.getUInt();
				curReality = reality.getString();
				balance = v["freeMargin"].getNumber();
				break;
			}
		}
		if (!curLogin) throw std::runtime_error("Can't find specified account (API key is invalid)");
		updatePositions();
	}
}

inline void Interface::onInit() {
}

inline Interface::SymbolInfo& Interface::getSymbolInfo(const std::string& symbol) {
	if (smbinfo.empty()) updateSymbols();
	auto iter = smbinfo.find(symbol);
	if (iter == smbinfo.end()) {
		throw std::runtime_error("Unknown symbol");
	}
	return iter->second;
}

inline void Interface::updateSymbols() {
	if (smbinfo.empty()) {
		login();
		//login can call this function recursuvely - no need to reupdate again
		if (!smbinfo.empty()) return;
	} else {
		login();
	}
	smbinfo.clear();
	Value symbs = hjsn_utils.GET("/utils/instruments.json");
	for (Value z : symbs) {
		std::string symbol = z["symbol"].getString();
		std::string curSymb = z["priceCurrency"].getString();
		smbinfo.emplace(symbol, SymbolInfo {
			symbol,
			curSymb,
			z["contractSize"].getNumber(),
			z["step"].getNumber(),
		});
		curinfo[curSymb];
	}

}

inline bool Interface::hasKey() const {
	return !(authKey.empty() || authSecret.empty() || authAccount.empty() || curLogin == 0);
}

inline void Interface::updatePositions() {
	Value data = getData(hjsn.POST("/api/v3/trading/orders/active", Object
			("login",curLogin)
			("reality", curReality)
	));

	for (auto &&x : smbinfo) {
		x.second.position = 0;
	}

	Value morders = data["marketOrders"];
	for (Value v : morders) {
		std::string symbol = v["symbol"].getString();
		SymbolInfo &sinfo = getSymbolInfo(symbol);
		double volume = v["volume"].getNumber() * sinfo.mult;
		if (v["side"].getString() == "SELL") volume = -volume;
		sinfo.position += volume;
		updateRate(sinfo, v);
	}

}

inline void Interface::updateRate(const SymbolInfo &sinfo, Value order) {
	double rate = first_match([](Value v){return v.getNumber();}, order["closeConversionRate"], order["openConversionRate"]).getNumber();
	if (rate) rate = 1/rate;
	CurrencyInfo &cinfo = curinfo[sinfo.currency_symbol];
	cinfo.convRate = rate;

}
