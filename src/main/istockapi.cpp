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
						assets = assets*(1+fees);
					}
					break;
	}

	if (invert_price) price = 1/(adjValue(1/price, currency_step, rounded));
	else price = adjValue(price, currency_step, rounded);
	assets = adjValue(assets, asset_step, nearZero);
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

json::Value IStockApi::MarketInfo::toJSON() const {
return json::Object({
	{"asset_step",asset_step},
	{"currency_step", currency_step},
	{"asset_symbol",asset_symbol},
	{"currency_symbol", currency_symbol},
	{"min_size", min_size},
	{"min_volume", min_volume},
	{"fees", fees},
	{"feeScheme",strFeeScheme[feeScheme]},
	{"leverage", leverage},
	{"invert_price", invert_price},
	{"inverted_symbol", inverted_symbol},
	{"simulator", simulator},
	{"private_chart", private_chart},
	{"wallet_id", wallet_id}});
}
IStockApi::MarketInfo IStockApi::MarketInfo::fromJSON(const json::Value &v) {
	MarketInfo res;
	res.asset_step = v["asset_step"].getNumber();
	res.currency_step = v["currency_step"].getNumber();
	res.asset_symbol = v["asset_symbol"].getString();
	res.currency_symbol = v["currency_symbol"].getString();
	res.min_size = v["min_size"].getNumber();
	res.min_volume= v["min_volume"].getNumber();
	res.fees = v["fees"].getNumber();
	res.feeScheme = strFeeScheme[v["feeScheme"].getString()];
	res.leverage= v["leverage"].getNumber();
	res.invert_price= v["invert_price"].getBool();
	res.simulator= v["simulator"].getBool();
	res.inverted_symbol= v["inverted_symbol"].getString();
	res.private_chart = v["private_chart"].getBool();
	res.wallet_id = v["wallet_id"].getString();
	return res;
}

std::int64_t IStockApi::MarketInfo::priceToTick(double price) const {
	if (invert_price) {
		return -static_cast<std::int64_t>(std::round((1.0/price)/currency_step));
	} else {
		return static_cast<std::int64_t>(std::round(price/currency_step));
	}
}
double IStockApi::MarketInfo::tickToPrice(std::int64_t tick) const {
	if (invert_price) {
		return -currency_step/tick;
	} else {
		return tick*currency_step;
	}
}


