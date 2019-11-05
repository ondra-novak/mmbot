/*
 * api.cpp
 *
 *  Created on: 8. 6. 2019
 *      Author: ondra
 */

#include <fstream>
#include "api.h"

#include <sys/stat.h>
#include <unordered_map>
#include <imtjson/string.h>
#include <imtjson/array.h>
#include <imtjson/object.h>
#include <shared/linear_map.h>
#include <imtjson/binjson.tcc>
#include <imtjson/binary.h>

#include "../main/istockapi.cpp"
using namespace json;




static Value getBalance(AbstractBrokerAPI &handler, const Value &request) {
	return handler.getBalance(request.getString());
}

static Value syncTrades(AbstractBrokerAPI &handler, const Value &request) {
	AbstractBrokerAPI::TradesSync hst(
			handler.syncTrades(
					request["lastId"],
					request["pair"].getString()));
	Array response;
	response.reserve(hst.trades.size());
	for (auto &&itm: hst.trades) {
		response.push_back(itm.toJSON());
	}
	return Object("trades",response)
				 ("lastId", hst.lastId);
}

static Value getOpenOrders(AbstractBrokerAPI &handler, const Value &request) {
	AbstractBrokerAPI::Orders ords(handler.getOpenOrders(request.getString()));

	Array response;
	response.reserve(ords.size());
	for (auto &&itm:ords) {
		response.push_back(Object
				("id",itm.id)
				("clientOrderId",itm.client_id)
				("size",itm.size)
				("price",itm.price));
	}
	return response;
}

static Value getTicker(AbstractBrokerAPI &handler, const Value &req) {
	AbstractBrokerAPI::Ticker tk(handler.getTicker(req.getString()));

	return Object
			("bid", tk.bid)
			("ask", tk.ask)
			("last", tk.last)
			("timestamp",tk.time);
}


static Value placeOrder(AbstractBrokerAPI &handler, const Value &req) {
	return handler.placeOrder(req["pair"].getString(),
			req["size"].getNumber(),
			req["price"].getNumber(),
			req["clientOrderId"],
			req["replaceOrderId"],
			req["replaceOrderSize"].getNumber());
}

static Value enableDebug(AbstractBrokerAPI &handler, const Value &req) {
	AbstractBrokerAPI *h = dynamic_cast<AbstractBrokerAPI *>(&handler);
	if (h) {
		h->enable_debug(req.getBool());
	}
	return Value();
}


static Value getBrokerInfo(AbstractBrokerAPI &handler, const Value &req) {
	AbstractBrokerAPI::BrokerInfo nfo = handler.getBrokerInfo();
	return Object("name",nfo.exchangeName)
				 ("url",nfo.exchangeUrl)
				 ("version",nfo.version)
				 ("licence",nfo.licence)
				 ("trading_enabled", nfo.trading_enabled)
				 ("settings",nfo.settings)
				 ("favicon",Value(BinaryView(StrViewA(nfo.favicon)),base64));
}

static Value reset(AbstractBrokerAPI &handler, const Value &req) {
	handler.reset();
	return Value();
}

static Value getAllPairs(AbstractBrokerAPI &handler, const Value &req) {
	auto r = handler.getAllPairs();
	Array response;
	response.reserve(r.size());
	for (auto &&itm: r) response.push_back(itm);
	return response;
}

static Value getFees(AbstractBrokerAPI &handler, const Value &req) {
	return  handler.getFees(req.getString());
}


static Value getInfo(AbstractBrokerAPI &handler, const Value &req) {
	AbstractBrokerAPI::MarketInfo nfo ( handler.getMarketInfo(req.getString()) );
	return Object
			("asset_step",nfo.asset_step)
			("currency_step", nfo.currency_step)
			("asset_symbol",nfo.asset_symbol)
			("currency_symbol", nfo.currency_symbol)
			("min_size", nfo.min_size)
			("min_volume", nfo.min_volume)
			("fees", nfo.fees)
			("feeScheme",AbstractBrokerAPI::strFeeScheme[nfo.feeScheme])
			("leverage", nfo.leverage)
			("invert_price", nfo.invert_price)
			("inverted_symbol", nfo.inverted_symbol)
			("simulator", nfo.simulator);
}

static Value setApiKey(AbstractBrokerAPI &handler, const Value &req) {
	handler.setApiKey(req);
	return Value();
}

static Value getApiKeyFields(AbstractBrokerAPI &handler, const Value &req) {
	return handler.getApiKeyFields();
}

static Value setSettings(AbstractBrokerAPI &handler, const Value &req) {
	handler.setSettings(req);;
	return Value();
}

static Value getSettings(AbstractBrokerAPI &handler, const Value &req) {
	return handler.getSettings(req.toString().str());
}


///Handler function
using HandlerFn = Value (*)(AbstractBrokerAPI &handler, const Value &request);
using MethodMap = ondra_shared::linear_map<std::string_view, HandlerFn> ;

static MethodMap methodMap ({
			{"getBalance",&getBalance},
			{"syncTrades",&syncTrades},
			{"getOpenOrders",&getOpenOrders},
			{"getTicker",&getTicker},
			{"placeOrder",&placeOrder},
			{"reset",&reset},
			{"getAllPairs",&getAllPairs},
			{"getFees",&getFees},
			{"getInfo",&getInfo},
			{"enableDebug",&enableDebug},
			{"getBrokerInfo",&getBrokerInfo},
			{"setApiKey",&setApiKey},
			{"getApiKeyFields",&getApiKeyFields},
			{"setSettings",&setSettings},
			{"getSettings",&getSettings}
	});


Value callMethod(AbstractBrokerAPI &api, std::string_view name, Value args) {
	try {
		auto iter = methodMap.find(name);
		if (iter == methodMap.end()) throw std::runtime_error("Method not implemented");
		return {true, (*iter->second)(api, args)};
	} catch (Value &e) {
		return {false, e};
	} catch (std::exception &e) {
		return {false, e.what()};
	}
}


void AbstractBrokerAPI::dispatch(std::istream& input, std::ostream& output, AbstractBrokerAPI &handler) {

	try {
		Value v = Value::fromStream(input);
		handler.loadKeys();
		handler.onInit();
		while (true) {
			callMethod(handler, v[0].getString(), v[1]).toStream(output);
			output << std::endl;
			int i = input.get();
			while (i != EOF && isspace(i)) i = input.get();
			if (i == EOF) break;
			input.putback(i);
			v = Value::fromStream(input);
		}
	} catch (std::exception &e) {
		Value({false, e.what()}).toStream(output);
		output << std::endl;
	}
}

AbstractBrokerAPI::AbstractBrokerAPI(const std::string &secure_storage_path,
		const Value &apiKeyFormat)
:secure_storage_path(secure_storage_path),apiKeyFormat(apiKeyFormat) {

}


void AbstractBrokerAPI::loadKeys() {
	try {
		std::ifstream f(secure_storage_path);
		if (!f) return;
		Value key = json::Value::parseBinary([&] {
			return f.get();
		}, base64);
		onLoadApiKey(key);
	} catch (std::exception &e) {
		std::cerr << e.what();
	}
}

void AbstractBrokerAPI::dispatch() {
	dispatch(std::cin, std::cout, *this);
}

void AbstractBrokerAPI::setApiKey(json::Value keyData) {

	onLoadApiKey(keyData);

	umask( S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	std::ofstream f(secure_storage_path);
	if (!f) throw std::runtime_error("Failed to store API key");
	keyData.serializeBinary([&](char c){f.put(c);}, compressKeys);
	if (!f) throw std::runtime_error("Failed to store API key");
}

json::Value AbstractBrokerAPI::getApiKeyFields() const {
	return apiKeyFormat;
}

json::Value AbstractBrokerAPI::getSettings(const std::string_view & ) const {
	throw std::runtime_error("unsupported");
}

void AbstractBrokerAPI::setSettings(json::Value) {
	throw std::runtime_error("unsupported");
}
