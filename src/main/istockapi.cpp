/*
 * istockapi.cpp
 *
 *  Created on: 24. 5. 2019
 *      Author: ondra
 */


#include "istockapi.h"

#include <cmath>
#include <imtjson/object.h>
#include "sgn.h"

IStockApi::Trade IStockApi::Trade::fromJSON(json::Value x) {

	json::Value f = x["fee"];
	double size = x["size"].getNumber();
	double price = x["price"].getNumber();

	if (f.defined()) {
		double fee = x["fee"].getNumber();

		return IStockApi::Trade {
			x["id"].stripKey(),
			x["time"].getUInt(),
			size,
			price,
			size,
			price+fee/size
		};
	} else {
		return IStockApi::Trade {
			x["id"].stripKey(),
			x["time"].getUInt(),
			size,
			price,
			x["eff_size"].getNumber(),
			x["eff_price"].getNumber()
		};
	}

}

json::Value IStockApi::Trade::toJSON() const {
	json::Object obj;

	obj("size", size)
	   ("time",time)
	   ("price",price)
	   ("eff_price",eff_price)
	   ("eff_size",eff_size)
	   ("id",id);
	return obj;
}


json::NamedEnum<IStockApi::FeeScheme> IStockApi::strFeeScheme ({
	{IStockApi::currency, "currency"},
	{IStockApi::assets, "assets"},
	{IStockApi::income, "income"},
	{IStockApi::outcome, "outcome"}
});

static double awayZero(double v) {
	if (v < 0) return floor(v);
	else return ceil(v);
}

void IStockApi::MarketInfo::addFees(double &assets, double &price) const {
	switch (feeScheme) {
	case IStockApi::currency:
				   price = price*(1 - sgn(assets)*fees);
				   break;
	case IStockApi::assets:
					assets = assets*(1+fees);
					break;
	case IStockApi::income:
					if (assets>0 ) assets = assets*(1+fees);
					else price = price*(1+fees);
					break;
	case IStockApi::outcome:
					if (assets<0 ) assets = assets*(1-fees);
					else price = price*(1-fees);
	}
	price = adjValue(price, currency_step, round);

	if (assets < min_size && assets > -min_size)
		assets = sgn(assets)*min_size;
	double vol = assets * price;
	if (vol < min_volume && vol > -min_volume)
		assets = sgn(assets)*(min_volume/price);

	assets = adjValue(assets, asset_step, awayZero);
}

void IStockApi::MarketInfo::removeFees(double &assets, double &price) const {
	switch (feeScheme) {
	case IStockApi::currency:
				   price = price*(1+ sgn(assets)*fees);
				   break;
	case IStockApi::assets:
					assets = assets*(1-fees);
					break;
	case IStockApi::income:
					if (assets>0 ) assets = assets*(1-fees);
					else price = price*(1-fees);
					break;
	case IStockApi::outcome:
					if (assets<0 ) assets = assets*(1+fees);
					else price = price*(1+fees);
	}
}

IStockApi::TradeWithBalance IStockApi::TradeWithBalance::fromJSON(json::Value v) {
	json::Value jbal = v["bal"];
	double bal = jbal.defined()?jbal.getNumber():no_balance;
	TradeWithBalance ret(Trade::fromJSON(v),bal);
	return ret;
}

json::Value IStockApi::TradeWithBalance::toJSON() const {
	json::Value v = Trade::toJSON();
	return v.replace("bal", balance);
}
