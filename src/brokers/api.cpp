/*
 * api.cpp
 *
 *  Created on: 8. 6. 2019
 *      Author: ondra
 */

#include <iostream>
#include "api.h"

#include <unordered_map>
#include <imtjson/string.h>
#include <imtjson/array.h>
#include <imtjson/object.h>
#include <shared/linear_map.h>

#include "../main/istockapi.cpp"
using namespace json;




static Value getBalance(IStockApi &handler, const Value &request) {
	return handler.getBalance(request.getString());
}

static Value getTrades(IStockApi &handler, const Value &request) {
	IStockApi::TradeHistory hst(
			handler.getTrades(
					request["lastId"],
					request["fromTime"].getUInt(),
					request["pair"].getString()));
	Array response;
	response.reserve(hst.size());
	for (auto &&itm: hst) {
		response.push_back(itm.toJSON());
	}
	return response;
}

static Value getOpenOrders(IStockApi &handler, const Value &request) {
	IStockApi::Orders ords(handler.getOpenOrders(request.getString()));

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

static Value getTicker(IStockApi &handler, const Value &req) {
	IStockApi::Ticker tk(handler.getTicker(req.getString()));

	return Object
			("bid", tk.bid)
			("ask", tk.ask)
			("last", tk.last)
			("timestamp",tk.time);
}


static Value placeOrder(IStockApi &handler, const Value &req) {
	return handler.placeOrder(req["pair"].getString(),
			req["size"].getNumber(),
			req["price"].getNumber(),
			req["clientOrderId"],
			req["replaceOrderId"],
			req["replaceOrderSize"].getNumber());
}

static Value enableDebug(IStockApi &handler, const Value &req) {
	AbstractBrokerAPI *h = dynamic_cast<AbstractBrokerAPI *>(&handler);
	if (h) {
		h->enable_debug(req.getBool());
	}
	return Value();
}


static Value reset(IStockApi &handler, const Value &req) {
	handler.reset();
	return Value();
}

static Value getAllPairs(IStockApi &handler, const Value &req) {
	auto r = handler.getAllPairs();
	Array response;
	response.reserve(r.size());
	for (auto &&itm: r) response.push_back(itm);
	return response;
}

static Value getFees(IStockApi &handler, const Value &req) {
	return  handler.getFees(req.getString());
}


static Value getInfo(IStockApi &handler, const Value &req) {
	IStockApi::MarketInfo nfo ( handler.getMarketInfo(req.getString()) );
	return Object
			("asset_step",nfo.asset_step)
			("currency_step", nfo.currency_step)
			("asset_symbol",nfo.asset_symbol)
			("currency_symbol", nfo.currency_symbol)
			("min_size", nfo.min_size)
			("min_volume", nfo.min_volume)
			("fees", nfo.fees)
			("feeScheme",IStockApi::strFeeScheme[nfo.feeScheme])
			("leverage", nfo.leverage);
}


///Handler function
using HandlerFn = Value (*)(IStockApi &handler, const Value &request);
using MethodMap = ondra_shared::linear_map<std::string_view, decltype(&getBalance)> ;

static MethodMap methodMap ({
			{"getBalance",&getBalance},
			{"getTrades",&getTrades},
			{"getOpenOrders",&getOpenOrders},
			{"getTicker",&getTicker},
			{"placeOrder",&placeOrder},
			{"reset",&reset},
			{"getAllPairs",&getAllPairs},
			{"getFees",&getFees},
			{"getInfo",&getInfo},
			{"enableDebug",&enableDebug}
	});


Value callMethod(IStockApi &api, std::string_view name, Value args) {
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


void AbstractBrokerAPI::dispatch(std::istream& input, std::ostream& output, IStockApi &handler) {



	while (true) {
		int i = input.get();
		if (i == EOF) return;
		input.putback(i);
		Value v = Value::fromStream(input);
		callMethod(handler, v[0].getString(), v[1]).toStream(output);
		output << std::endl;
	}

}

void AbstractBrokerAPI::dispatch() {
	dispatch(std::cin, std::cout, *this);
}
