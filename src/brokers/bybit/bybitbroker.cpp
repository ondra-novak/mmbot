/*
 * bybitbroker.cpp
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */

#include "bybitbroker.h"
#include <imtjson/object.h>
#include <imtjson/parser.h>

const std::string licence(R"mit(Copyright (c) 2020 Ondřej Novák

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
OTHER DEALINGS IN THE SOFTWARE.)mit");

const std::string icon(
		"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"
		"2aoaAAAAAElFTkSuQmCC"
);

using namespace json;

 Value APIKeyFormat({
		Object{
			{"label","API Key"},
			{"name","key"},
			{"type","string"}
		},
		Object{
			{"label","API Secret"},
			{"name","secret"},
			{"type","string"}
		},
		Object{
			{"label","Server"},
			{"name","server"},
			{"type","enum"},
			{"options",Object{
				{"live","Live"},
				{"testnet","Testnet (paper trading)"},
			}}
		}
});


ByBitBroker::ByBitBroker(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path, APIKeyFormat)
 	 ,curTime(15)
 	 ,api(simpleServer::HttpClient("MMBOT (+https://www.mmbot.trade)", simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(15)),"https://api.bybit.com")
{

}

json::Value ByBitBroker::testCall(const std::string_view &method, json::Value args) {
	return nullptr;
}

std::vector<std::string> ByBitBroker::getAllPairs() {
	return {};
}

IStockApi::MarketInfo ByBitBroker::getMarketInfo(const std::string_view &pair) {
	return {};
}

AbstractBrokerAPI* ByBitBroker::createSubaccount(const std::string &secure_storage_path) {
	return new ByBitBroker(secure_storage_path);
}

IStockApi::BrokerInfo ByBitBroker::getBrokerInfo() {
	return IStockApi::BrokerInfo{
		hasKeys(),
		"bybit",
		"ByBit",
		"https://www.bybit.com/en-US/invite?ref=Y6ERW0",
		"1.0",
		licence,
		icon,
		false,
		true,
	};
}

void ByBitBroker::onLoadApiKey(json::Value keyData) {
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();
	testnet = keyData["type"].getString() == "testnet";
	if (testnet) {
		api.setBaseUrl("https://testnet-api.bybit.com");
	} else {
		api.setBaseUrl("https://api.bybit.com");
	}
}

static std::string_view extractExpiration(std::string_view str) {
	auto l = str.length();
	while (l>0) {
		if (!isdigit(str[l-1])) return str.substr(l);
		l--;
	}
	return std::string_view();
}

json::Value ByBitBroker::getMarkets() const {
	Object inversed;
	Object usdt;
	ondra_shared::linear_map<std::string_view, Object> exp_futures;
	updateSymbols();
	for(const auto &sdef: symbols) {
		if (sdef.second.invert_price) {
			auto label= sdef.second.currency_symbol+"/"+sdef.second.inverted_symbol;
			if (!sdef.second.expiration.empty()) {
				exp_futures[sdef.second.expiration].set(label, sdef.first);
			} else {
				inversed.set(label, sdef.first);
			}
		} else {
			usdt.set(sdef.second.asset_symbol+"/"+sdef.second.currency_symbol, sdef.first);
		}
	}
	return Object{
		{"Inverse Perpetual", inversed},
		{"USDT Perpetual", usdt},
		{"Inverse Futures", Value(json::object, exp_futures.begin(), exp_futures.end(),[](const auto &item){
			return Value(item.first, item.second);
		})
		}
	};
}

double ByBitBroker::getBalance(const std::string_view &symb, const std::string_view &pair) {
	return 0;
}

IStockApi::TradesSync ByBitBroker::syncTrades(json::Value lastId, const std::string_view &pair) {
	return {};
}

bool ByBitBroker::reset() {
	return true;
}

IStockApi::Orders ByBitBroker::getOpenOrders(const std::string_view &par) {
	return {};
}

json::Value ByBitBroker::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	throw std::runtime_error("not implemented");
}

double ByBitBroker::getFees(const std::string_view &pair) {
	return 0;
}

IStockApi::Ticker ByBitBroker::getTicker(const std::string_view &piar) {
	return {};
}

void ByBitBroker::onInit() {

}

IBrokerControl::AllWallets ByBitBroker::getWallet() {
	return {};
}

bool ByBitBroker::hasKeys() const {
	return !api_key.empty() && !api_secret.empty();
}

void ByBitBroker::updateSymbols() const {
	auto now = std::chrono::steady_clock::now();
	if (symbols_expiration>now) return;
	forceUpdateSymbols();
	symbols_expiration = now;
}

void ByBitBroker::handleError(json::Value err) {
	Value ret_code = err["ret_code"];
	Value ret_msg = err["ret_msg"];
	if (ret_code.defined()) {
		std::string msg(ret_code.toString().str());
		msg.push_back(' ');
		msg.append(ret_msg.getString());
		throw std::runtime_error(msg);
	} else {
		throw std::runtime_error(err.toString().str());
	}
}

void ByBitBroker::handleException(const HTTPJson::UnknownStatusException &ex) {
	auto s = ex.response.getBody();
	try {
		Value r (Value::parse(s));
		handleError(r);
	} catch (const ParseError &er) {
	}
	throw std::runtime_error(std::to_string(ex.code)+" "+ex.message);
}

Value ByBitBroker::publicGET(std::string_view uri) const {


	try {
		Value ret = api.GET(uri);
		Value ret_code = ret["ret_code"];
		if (ret_code.getUInt() != 0) handleError(ret);
		return ret["result"];
	} catch (const HTTPJson::UnknownStatusException &ex) {
		handleException(ex);
		throw;
	}
}

void ByBitBroker::forceUpdateSymbols() const {
	Value smblist = publicGET("/v2/public/symbols");
	SymbolMap::Set::VecT symbolvect;
	for (Value smb: smblist) {
		std::string_view name = smb["name"].getString();
		bool inverted = isInverted(name);
		MarketInfoEx nfo ;
		nfo.asset_step = smb["lot_size_filter"]["qty_step"].getNumber();
		nfo.min_size = smb["lot_size_filter"]["min_trading_qty"].getNumber();
		nfo.min_volume = 0;
		nfo.currency_step = smb["price_filter"]["tick_size"].getNumber();
		nfo.feeScheme = currency;
		nfo.fees = 0;
		nfo.leverage = smb["leverage_filter"]["max_leverage"].getNumber();
		nfo.private_chart = false;
		nfo.simulator = testnet;
		nfo.wallet_id = "futures";
		nfo.alias = smb["alias"].getString();
		nfo.expiration = extractExpiration(nfo.alias);
		if (inverted) {
			nfo.asset_symbol = smb["quote_currency"].getString();
			nfo.currency_symbol = smb["base_currency"].getString();
			nfo.invert_price = true;
			nfo.inverted_symbol = nfo.asset_symbol;
		} else {
			nfo.asset_symbol = smb["base_currency"].getString();
			nfo.currency_symbol = smb["quote_currency"].getString();
			nfo.invert_price = false;
		}
		symbolvect.push_back(SymbolMap::value_type{
			std::string(name),std::move(nfo)
		});
	}
	symbols = SymbolMap(std::move(symbolvect));


}

bool ByBitBroker::isInverted(const std::string_view &name) const {
	return name.substr(name.size()-4) != "USDT"; //no other way how to detect inversed symbol
}

const ByBitBroker::MarketInfoEx& ByBitBroker::getSymbol(const std::string_view &name) const {
	updateSymbols();
	auto iter = symbols.find(name);
	if (iter == symbols.end()) throw std::runtime_error(std::string("Unknown symbol: ").append(name));
	else return iter->second;
}
