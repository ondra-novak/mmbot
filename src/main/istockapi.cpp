/*
 * istockapi.cpp
 *
 *  Created on: 24. 5. 2019
 *      Author: ondra
 */


#include "istockapi.h"

#include <cmath>
#include <imtjson/object.h>
#include <shared/logOutput.h>
#include "sgn.h"

using ondra_shared::logDebug;

IStockApi::Trade IStockApi::Trade::fromJSON(json::Value x) {
	return IStockApi::Trade {
		x["id"].stripKey(),
		x["time"].getUIntLong(),
		x["size"].getNumber(),
		x["price"].getNumber(),
		x["eff_size"].getNumber(),
		x["eff_price"].getNumber(),
		x["order_id"]
	};
}


json::Value IStockApi::Trade::toJSON() const {

	return json::Object ({
		{"size", size},
		{"time",time},
		{"price",price},
		{"eff_price",eff_price},
		{"eff_size",eff_size},
		{"id",id},
		{"order_id",order_id}
	});
}



IStockApi::TradeWithBalance IStockApi::TradeWithBalance::fromJSON(json::Value v) {
	json::Value jbal = v["bal"];
	json::Value jman = v["man"];
	double bal = jbal.defined()?jbal.getNumber():NaN;
	TradeWithBalance ret(Trade::fromJSON(v),bal,jman.getBool());
	return ret;
}

json::Value IStockApi::TradeWithBalance::toJSON() const {
	json::Value v = Trade::toJSON();
	json::Object edt(v);
	edt.set("bal", balance);
	edt.set("man", manual_trade);
	return edt;
}

IStockApi::Order IStockApi::Order::fromJSON(json::Value v) {
	return IStockApi::Order {
		v["id"],
		v["client_id"],
		v["size"].getNumber(),
		v["price"].getNumber()
	};
}

json::Value IStockApi::Order::toJSON() const {
	return json::Object({
		{"id",id},
		{"client_id",client_id},
		{"size",size},
		{"price",price}
	});
}

