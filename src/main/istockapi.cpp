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

	json::Value f = x["fee"];
	double size = x["size"].getNumber();
	double price = x["price"].getNumber();

	if (f.defined()) {
		double fee = x["fee"].getNumber();

		return IStockApi::Trade {
			x["id"].stripKey(),
			x["time"].getUIntLong(),
			size,
			price,
			size,
			price+fee/size
		};
	} else {
		return IStockApi::Trade {
			x["id"].stripKey(),
			x["time"].getUIntLong(),
			size,
			price,
			x["eff_size"].getNumber(),
			x["eff_price"].getNumber()
		};
	}

}

json::Value IStockApi::Trade::toJSON() const {

	return json::Object ({
		{"size", size},
		{"time",time},
		{"price",price},
		{"eff_price",eff_price},
		{"eff_size",eff_size},
		{"id",id}
	});
}


json::NamedEnum<IStockApi::FeeScheme> IStockApi::strFeeScheme ({
	{IStockApi::currency, "currency"},
	{IStockApi::assets, "assets"},
	{IStockApi::income, "income"},
	{IStockApi::outcome, "outcome"}
});
/*
static double awayZero(double v) {
	if (v < 0) return floor(v);
	else return ceil(v);
}
*/
static double nearZero(double v) {
	return sgn(v) * floor(std::abs(v));
}

static double rounded(double v) {
	return std::round(v);
}


void IStockApi::MarketInfo::addFees(double &assets, double &price) const {
	//always shift price
    price = price*(1 - sgn(assets)*fees);
	switch (feeScheme) {
	case IStockApi::currency:
				   break;
	case IStockApi::assets:
					assets = assets*(1+fees);
					break;
	case IStockApi::income:
					if (assets>0 ) {
						assets = assets*(1+fees);
					}
					break;
	case IStockApi::outcome:
					if (assets<0 ) {
						assets = assets*(1-fees);
					}
					break;
	}

	logDebug("Before round: $1 $2", assets, price);
	if (invert_price) price = 1/(adjValue(1/price, currency_step, rounded));
	else price = adjValue(price, currency_step, rounded);
	assets = adjValue(assets, asset_step, nearZero);
	logDebug("After round: $1 $2", assets, price);
}

void IStockApi::MarketInfo::removeFees(double &assets, double &price) const {
	   price = price/(1- sgn(assets)*fees);
	switch (feeScheme) {
	case IStockApi::currency:
				   break;
	case IStockApi::assets:
					assets = assets/(1+fees);
					break;
	case IStockApi::income:
					if (assets>0 ) assets = assets/(1+fees);
					break;
	case IStockApi::outcome:
					if (assets<0 ) assets = assets/(1+fees);
					break;
	}
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
