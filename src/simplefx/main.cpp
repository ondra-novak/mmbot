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
#include "httpjson.h"
#include "market.h"
#include "quotedist.h"
#include "quotestream.h"
#include "tradingengine.h"

using json::Object;
using json::Value;



static Value keyFormat = {Object
							("name","key")
							("type","string")
							("label","Key"),
						 Object
							("name","secret")
							("type","string")
							("label","Secret"),
						 Object
							("name","reality")
							("type","enum")
							("options",Object("LIVE","Live")("DEMO","Demo"))
							("label","Reality")
};

class Interface: public AbstractBrokerAPI {
public:

	simpleServer::HttpClient httpc;
	HTTPJson hjsn;
	std::unique_ptr<QuoteStream> qstream;
	std::unique_ptr<Market> market;


	double execCommand(const std::string &symbol, double amount);

	PTradingEngine getEngine(const std::string_view &symbol) {
		if (market == nullptr) {
			PQuoteDistributor qdist = new QuoteDistributor();
			qstream = std::make_unique<QuoteStream>(httpc,"https://web-quotes.simplefx.com/signalr/", qdist->createReceiveFn());
			qdist->connect(qstream->connect());
			market = std::make_unique<Market>(qdist, [this](const std::string &symbol, double amount){
				return this->execCommand(symbol, amount);
			});
		}
		return market->getMarket(symbol);
	}


	void login();

	Interface(const std::string &path):AbstractBrokerAPI(path, keyFormat)
	,httpc("+mmbot/2.0 simplefx_broker (https://github.com/ondra-novak/mmbot)",
			simpleServer::newHttpsProvider())
	,hjsn(httpc,"https://simplefx.com") {}

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


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

inline double Interface::getBalance(const std::string_view &symb) {
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
	return {};
}

inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

inline std::vector<std::string> Interface::getAllPairs() {
	return {};
}

inline void Interface::enable_debug(bool enable) {

}

inline Interface::BrokerInfo Interface::getBrokerInfo() {
	return {
		false,
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

}

inline double Interface::execCommand(const std::string &symbol, double amount) {
	login();
	Value v = hjsn.POST("/api/v3/trading/orders/market",json::Object
			("Reality",authReality)
			("Login", authLogin)
			("Symbol", symbol)
			("Side",amount>0?"BUY":"SELL")
			("Volume", fabs(amount))
			("IsFIFO", true));

}

inline void Interface::login() {
	if (!hjsn.hasToken()) {
		Value v = hjsn.POST("/api/v3/auth/key", json::Object
				  ("clientId", authKey)
				  ("clientSecret",authSecret));
		Value data = v["data"];
		Value token = v["token"];
		hjsn.setToken(token.getString());
	}
}

inline void Interface::onInit() {
}

