/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <rpc/rpcServer.h>
#include <imtjson/operations.h>
#include "shared/toString.h"
#include "proxy.h"
#include "../main/istockapi.h"
#include <cmath>
#include <ctime>

#include "../brokers/api.h"
#include <imtjson/stringValue.h>
#include "../shared/linear_map.h"
#include "../shared/iterator_stream.h"
#include "../brokers/orderdatadb.h"
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>
#include "../main/sgn.h"

using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	Proxy px;

	Interface(const std::string &path):AbstractBrokerAPI(path, {
			Object
				("name","key")
				("label","ID")
				("type","string"),
			Object
				("name","secret")
				("label","Secret")
				("type","string"),
			Object
				("name","server")
				("label","Server")
				("type","enum")
				("options",Object
						("main","www.bitmex.com")
						("testnet","testnet.bitmex.com"))
				("default","main")})
	{}


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


	ondra_shared::linear_map<std::string, double, std::less<std::string_view> > tick_cache;

	ondra_shared::linear_map<std::string, json::Value, std::less<std::string_view> > openOrdersCache;

};




int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Requires a signle parametr" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();

}

inline double Interface::getBalance(const std::string_view &symb) {
	return 0;
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId,  const std::string_view &pair) {
	return {};
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {
	return {};

}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	return {};
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {
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

void Interface::enable_debug(bool enable) {
	px.debug = enable;
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"bitmex",
		"BitMEX",
		"https://www.bitmex.com/",
		"1.0",
		R"mit(Copyright (c) 2019 Ondřej Novák

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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAYAAADDPmHLAAAB1klEQVR42u3a0VECQRBF0dEyEb4x"
"K+IyK/kmFE2Bhemd7ulzEtBa3vbFKscAAAAAAAAAAAAAdvCx8oc/vq9/VR7U5fce9qyut8f053D/"
"uTz1+356B9aK+PCPMIANPfv2Lx1ApfO/Mxeg8fk3gARfAA2AZf1fNgD9dwH0P0H/DaB5/w2gef+X"
"DED/XQD9T9J/A2jefwNo3v/TB6D/LoD+J+q/ATTvvwE07/+pA9B/F0D/k/XfAJr33wCa9/+0Aei/"
"C6D/CftvAM37bwDN+3/KAPTfBdD/pP03gOb9N4Dm/Q8fgP67APqfuP9jjPGl1S6AD79p/0MHoP8u"
"gP4n778BUGsA+j+3/2ED0H8XQP8L9N8AqDMA/Z/f/5AB6L8LoP9F+m8A1BiA/sf0f/oA9N8F0P9C"
"/TcA8g9A/+P6P3UA+u8C6H+x/hsAuQeg/7H9H2PSfwVX63/FUy0BGIC3v9EA9D++/1MG4O9/FwAD"
"0H8D0P9y/X97APrvAmAA+m8A+l+y/28NQP9dAAxA/w1A/8v2/+UB6L8LgAHovwHof+n+vzQA/XcB"
"MAD9NwD9L9//wwPQfxcAA9B/A9D/Lfp/aAD67wJgAPpvAPq/Tf8BAAAAAAAAAAAA2MQ/A0e46eQ0"
"zFcAAAAASUVORK5CYII="
	};
}

inline std::vector<std::string> Interface::getAllPairs() {
	Value resp = px.request(false, "GET","/api/v1/instrument/active");
	std::vector<std::string> symbols;
	for (Value s : resp) {
		auto symb = s["symbol"].getString();
		if (s["optionUnderlyingPrice"].hasValue()) continue;
		if (s["settlCurrency"].getString() != "XBt") continue;
		if (s["isInverse"].getBool()) {
			if (s["underlying"].getString() != "XBT") continue;
			symbols.push_back(std::string("~").append(symb.data,symb.length));
		} else {
			if (s["quoteCurrency"].getString() != "XBT") continue;
			symbols.push_back(symb);
		}
	}
	return symbols;
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	px.setTestnet(keyData["server"].getString() == "testnet");
	px.privKey = keyData["secret"].getString();
	px.pubKey = keyData["key"].getString();
}

inline void Interface::onInit() {
	//empty
}
