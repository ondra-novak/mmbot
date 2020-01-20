/*
 * main.cpp
 *
 *  Created on: 13. 11. 2019
 *      Author: ondra
 */
#include <imtjson/array.h>
#include <vector>
#include <fstream>

#include "../brokers/api.h"
#include <imtjson/value.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>

#include "../shared/stdLogOutput.h"
#include "../shared/first_match.h"
#include "../shared/logOutput.h"
#include "../brokers/httpjson.h"
#include "../brokers/log.h"
#include "market.h"
#include "quotedist.h"
#include "quotestream.h"
#include "tradingengine.h"

using json::Object;
using json::String;
using json::Value;
using ondra_shared::first_match;
using ondra_shared::logDebug;



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
							("label","Default account ID")
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
		return getEngine(std::string(symbol));
	}
	PTradingEngine getEngine(const std::string &symbol) {
		try {
			if (market == nullptr) {
				PQuoteDistributor qdist = new QuoteDistributor();
				qstream = std::make_unique<QuoteStream>(httpc,"https://web-quotes.simplefx.com/signalr/", qdist->createReceiveFn());
				qdist->connect(qstream->connect());
				market = std::make_unique<Market>(qdist, [this](const std::string &symbol, double amount){
					auto z = this->execCommand(symbol, amount);
					return z;
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
			simpleServer::newHttpsProvider(),nullptr,simpleServer::newCachedDNSProvider(10))
	,hjsn(simpleServer::HttpClient(httpc),"https://rest.simplefx.com")
	,hjsn_utils(simpleServer::HttpClient(httpc),"https://simplefx.com")
	{
	}

	virtual double getBalance(const std::string_view & symb) override;
	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
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
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;
	virtual json::Value getSettings(const std::string_view & pairHint) const override;
	virtual json::Value setSettings(json::Value v) override;
	virtual void restoreSettings(json::Value v) override;
	virtual void setApiKey(json::Value keyData) override;

	std::string authKey;
	std::string authSecret;
	std::string authAccount;
	unsigned int defaultAccount;
	double refBalance;


	struct Account {
		std::string reality;
		unsigned int login;
		double balance;
		double main_conv_rate;
		std::string currency;
	};


	struct SymbolInfo {
		std::string symbol;
		std::string currency_symbol;
		std::string asset_symbol;
		double mult;
		double step;
		double price;
	};

	struct SymbolSettings {
		unsigned int account;
		SettlementMode settlMode;
	};


	mutable std::unordered_map<std::string, SymbolInfo> smbinfo;
	mutable std::unordered_map<std::string, double> position;
	mutable std::unordered_map<unsigned int, Account> accounts;
	mutable std::unordered_map<std::string, SymbolSettings> symcfg;
	mutable std::chrono::system_clock::time_point smbexpire;

	Account &getAccount(const std::string &symbol);
	Account &getAccount(unsigned int login);


	std::uint64_t tokenExpire = 0;
	std::string token;


	SymbolInfo &getSymbolInfo(const std::string &symbol);
	void updateSymbols();
	bool hasKey() const;
	void updatePositions();
	double findConvRate(std::string fromCurrency, std::string toCurrency);
	bool isSettlementActive(const std::string &pair) const;
	void checkSettlements();


	Value tokenHeader();


protected:
	void updatePosition(const std::string& symbol, double amount);
	std::string getSettingsFile();
};



int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required storage path" << std::endl;
		return 1;
	}

	try {

		Interface ifc(argv[1]);
		ifc.dispatch();
		exit(0);


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
	throw std::runtime_error("not supported");
}

double Interface::findConvRate(std::string fromCurrency, std::string toCurrency) {
	std::unordered_map<std::string, double> toTarget;
	toTarget[fromCurrency] = 1.0;
	std::size_t sz = -1;
	while (toTarget.find(toCurrency) == toTarget.end() && toTarget.size() != sz) {
		sz = toTarget.size();
		for (auto &&k : smbinfo) {
			auto iter = toTarget.find(k.second.asset_symbol);
			if (iter != toTarget.end()) {
				toTarget.emplace(k.second.currency_symbol, iter->second*k.second.price);
			}
			iter = toTarget.find(k.second.currency_symbol);
			if (iter != toTarget.end()) {
					toTarget.emplace(k.second.asset_symbol, iter->second/k.second.price);
			}
		}
	}
	auto res = toTarget.find(toCurrency);
	if (res == toTarget.end()) return 0;
	else return res->second;

}

inline double Interface::getBalance(const std::string_view& symb,
		const std::string_view& pair) {
	std::string symbol (symb);
	std::string p (pair);
	if (smbinfo.empty()) updateSymbols();
	Account &a = getAccount(p);
	if (symb == pair) {
		auto iter = position.find(symbol);
		if (iter == position.end()) return 0;
		else return iter->second;
	} else {
		return (a.balance/a.main_conv_rate)*findConvRate(a.currency, symbol);
	}
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId,
		const std::string_view &pair) {

	PTradingEngine eng = getEngine(pair);
	std::string uid = lastId.getString();
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

	std::string p(pair);
	if (isSettlementActive(p)) {
		throw std::runtime_error("Settlement in progress");
	}

	PTradingEngine eng = getEngine(p);
	if (replaceId.defined()) {
		eng->cancelOrder(replaceId.getString());
	}
	if (size) {
		return eng->placeOrder(price, size, clientId);
	}


	return nullptr;
}

inline bool Interface::reset() {
	login();
	checkSettlements();
	return true;
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	std::string symbol (pair);
	const SymbolInfo &sinfo = getSymbolInfo(symbol);
	bool isdemo;
	try {
		const Account &a = getAccount(symbol);
		isdemo = a.reality == "DEMO";
	} catch (...) {
		isdemo = true;
	}
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
		isdemo
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
"VICNcrZqbPk3iPS3R8BEpSn/RTLKKKN8t/wHuIduWNu27QwAAAAASUVORK5CYII=",
hasKey()
	};
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	authKey = keyData["key"].getString();
	authSecret = keyData["secret"].getString();
	authAccount = keyData["account"].getString();
}

inline Interface::Account& Interface::getAccount(const std::string& symbol) {
	auto iter = symcfg.find(symbol);
	unsigned int login;
	if (iter == symcfg.end()) login = defaultAccount;
	else login = iter->second.account;
	return getAccount(login);
}

inline Interface::Account& Interface::getAccount(unsigned int login) {
	auto iter2 = accounts.find(login);
	if (iter2 == accounts.end()) {
		iter2 = accounts.find(defaultAccount);
		if (iter2 == accounts.end()) {
			iter2 = accounts.begin();
			if (iter2 == accounts.end()) {
				throw std::runtime_error("Failed to get account");
			}
		}
	}
	return iter2->second;
}

json::NamedEnum<SettlementMode> strSettlementMode ({
	{SettlementMode::none,"none"},
	{SettlementMode::active,"active"},
	{SettlementMode::friday,"friday"}
});

inline json::Value Interface::getSettings(const std::string_view& pairHint) const {
	const_cast<Interface *>(this)->login();
	std::string ph(pairHint);
	SymbolSettings ss;
	auto iter = symcfg.find(ph);
	if (iter != symcfg.end())  ss = iter->second;
	else ss = SymbolSettings{defaultAccount, SettlementMode::none};

	return {Object
		("type","enum")
		("label","Symbol")
		("name","pairHint")
		("default", pairHint)
		("options", Object(pairHint,pairHint)),
		Object
			("type","enum")
			("label","Use Account")
			("name","account")
			("default",ss.account)
			("options",Value(json::object, accounts.begin(), accounts.end(),[](auto &x){
				Value l = x.second.login;
				Value bal = x.second.balance / x.second.main_conv_rate;
				String ls = l.toString();
				return Value(ls, String({ls," (",x.second.reality,") ",bal.toString()," ",x.second.currency}));
			})),
		Object
			("type", "enum")
			("label", "Settlement")
			("name","settlmode")
			("default", strSettlementMode[ss.settlMode])
			("options", Object
				("none","Disabled")
				("active","Now")
				("friday","Friday 21:05-22:05 UTC")
			)
	};
}


inline json::Value Interface::setSettings(json::Value v) {
	std::string pairHint = v["pairHint"].getString();
	unsigned int account = v["account"].getUInt();
	SettlementMode smode = strSettlementMode[v["settlmode"].getString()];
	if (account == defaultAccount && smode == SettlementMode::none) symcfg.erase(pairHint);
	else {
		symcfg[pairHint]={account, smode};
	}
	json::Array out;
	for (auto &x : symcfg) {
		out.push_back({x.first, x.second.account, strSettlementMode[x.second.settlMode] });
	}
	return out;
}

void Interface::restoreSettings(json::Value v) {
	for (Value item:v) {
		std::string pairHint = item[0].getString();
		unsigned int account = item[1].getUInt();
		StrViewA smode = item[2].getString();
		SettlementMode s = smode.empty()?SettlementMode::none:strSettlementMode[smode];
		symcfg[pairHint]={account, s};
	}
	remove(getSettingsFile().c_str());
}

inline void Interface::setApiKey(json::Value keyData) {
	AbstractBrokerAPI::setApiKey(keyData);
	remove(getSettingsFile().c_str());
	symcfg.clear();
}

void Interface::updatePosition(const std::string& symbol, double amount) {
	auto piter = position.find(symbol);
	if (piter == position.end())
		position.emplace(symbol, amount);
	else
		piter->second += amount;

	logDebug("Positions updated - $1 changed by $2, currently $3",
			symbol, amount, LogRange(position.begin(), position.end(), ","));

}

inline double Interface::execCommand(const std::string &symbol, double amount) {
	login();
	SymbolInfo &sinfo = getSymbolInfo(symbol);
	Account &a = getAccount(symbol);
	double adjamount = amount / sinfo.mult;
	Value v = getData(hjsn.POST("/api/v3/trading/orders/market",json::Object
			("Reality",a.reality)
			("Login", a.login)
			("Symbol", symbol)
			("Side",adjamount>0?"BUY":"SELL")
			("Volume", fabs(adjamount))
			("IsFIFO", true), tokenHeader()));

	Value morder = v["marketOrders"][0];
	Value order = morder["order"];
	updatePosition(symbol, amount);
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
	try {
		auto now = TradingEngine::now();
		if (hasKey() && (token.empty()|| tokenExpire < now)) {
			Value v = getData(hjsn.POST("/api/v3/auth/key", json::Object
					  ("clientId", authKey)
					  ("clientSecret",authSecret)));
			Value token = v["token"];
			this->token = token.getString();
			tokenExpire = now + 15*60000;
			Value alist = getData(hjsn.GET("/api/v3/accounts", tokenHeader()));
			accounts.clear();
			defaultAccount = 0;
			Value currencies = getData(hjsn.GET("/api/v3/currencies", tokenHeader()));
			for (Value v: alist) {
				Value login = v["login"];
				Value reality = v["reality"];
				Value currency = v["currency"];
				Value c = currencies.find([&](Value v){
					return v["currency"] == currency;
				});

				accounts.emplace(login.getUInt(),Account {
					reality.getString(),
					static_cast<unsigned int>(login.getUInt()),
					v["freeMargin"].getNumber(),
					c["multiplier"].getValueOrDefault(1.0),
					c["displayCurrency"].getValueOrDefault(currency).getString()
				});

				if (login.toString().str() == StrViewA(authAccount)) {
					defaultAccount = login.getUInt();
				}
			}
			if (!defaultAccount) throw std::runtime_error("Can't find specified account (API key is invalid)");
			updatePositions();
		}
	} catch (const simpleServer::HTTPStatusException &e) {
		if (e.getStatusCode() == 409) throw std::runtime_error("Invalid API key");
		else throw;
	}
}

inline Value Interface::tokenHeader() {
	if (!token.empty()) {
		return Object("Authorization","bearer "+ token);
	} else {
		return Value();
	}
}

inline bool Interface::isSettlementActive(const std::string &pair) const {
	auto iter = symcfg.find(pair);
	if (iter == symcfg.end()) return false;
	SettlementMode sm = iter->second.settlMode;
	switch (sm) {
	default:
	case SettlementMode::none: return false;
	case SettlementMode::active: return true;
	case SettlementMode::friday: {
		std::hash<std::string> h;
		auto now = time(nullptr);
		auto week = (now/6048000)*6048000; //align to begin of week
		std::string digest = this->authKey+pair+std::to_string(week);
		int rndNum = h(digest)%2000; //generate deterministic random number 0-2000 sec(21:05-21:38)
		auto beg = week+162300+rndNum; //beg Friday 21:05-21:38
		auto end = week+166000;			//end Friday 22:13
		return now >= beg && now <=end; //settlement is enabled when current time is between
		}
	}
}

inline void Interface::checkSettlements() {
	for (auto &&x:position) {
		if (x.second) {
			if (isSettlementActive(x.first)) {
				PTradingEngine engine = getEngine(x.first);
				engine->runSettlement(-x.second);
			}
		}
	}
}

std::string Interface::getSettingsFile() {
	return this->secure_storage_path + ".conf";
}

inline void Interface::onInit() {
	std::ifstream settings(getSettingsFile());
	if (!settings) return;
	Value data = Value::fromStream(settings);
	symcfg.clear();
	for (Value k: data) {
		symcfg.emplace(k[0].getString(), SymbolSettings{static_cast<unsigned int>(k[1].getUInt()), SettlementMode::none});
	}


}

inline Interface::SymbolInfo& Interface::getSymbolInfo(const std::string& symbol) {
	auto now = std::chrono::system_clock::now();
	if (smbinfo.empty() || smbexpire < now) {
		updateSymbols();
		smbexpire = now+std::chrono::minutes(55);
	}
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
		std::string assSymb = z["marginCurrency"].getString();
		double quote = sqrt(z["quote"]["a"].getNumber()*z["quote"]["b"].getNumber());
		smbinfo.emplace(symbol, SymbolInfo {
			symbol,
			curSymb,
			assSymb,
			z["contractSize"].getNumber(),
			z["step"].getNumber(),
			quote
		});
	}

}

inline bool Interface::hasKey() const {
	return !(authKey.empty() || authSecret.empty() || authAccount.empty());
}

inline void Interface::updatePositions() {
	//save current position in case of network failure
	decltype(this->position) save;
	std::swap(save, this->position);

	try {

		for (auto &a : accounts) {
			Value data = getData(hjsn.POST("/api/v3/trading/orders/active", Object
						("login",a.second.login)
						("reality", a.second.reality), tokenHeader()
				));

				Value morders = data["marketOrders"];
				for (Value v : morders) {
					std::string symbol = v["symbol"].getString();
					SymbolInfo &sinfo = getSymbolInfo(symbol);
					double volume = v["volume"].getNumber() * sinfo.mult;
					if (v["side"].getString() == "SELL") volume = -volume;
					updatePosition(symbol, volume);
			}
		}
	} catch (...) {
		//if exception thrown, restore position state
		std::swap(save, this->position);
		throw;
	}
}

