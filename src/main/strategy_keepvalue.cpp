/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue.h"

#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

using ondra_shared::logDebug;

#include "../shared/logOutput.h"
std::string_view Strategy_KeepValue::id = "keepvalue";

Strategy_KeepValue::Strategy_KeepValue(const Config& cfg, double p, double a)
:cfg(cfg),p(p),a(a)
{
}

bool Strategy_KeepValue::isValid() const {
	return p >0 && (a+cfg.ea) > 0;
}

IStrategy* Strategy_KeepValue::init(double curPrice, double assets, double) const {
	return new Strategy_KeepValue(cfg, curPrice , assets);
}

std::pair<Strategy_KeepValue::OnTradeResult, IStrategy*> Strategy_KeepValue::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {


	double cf = (assetsLeft-tradeSize+cfg.ea)*(tradePrice - p);
	double p = this->p;
	if (tradeSize == 0) p = tradePrice;
	double k = (a+cfg.ea) * p;
	double nv = k * std::log(tradePrice/p);
	double pf = cf - nv;
	double ap = (pf / tradePrice) * cfg.accum;
	double np = pf * (1.0 - cfg.accum);
	double new_a = (k / tradePrice) - cfg.ea;
	return {
		OnTradeResult{np,ap}, new Strategy_KeepValue(cfg, tradePrice, new_a+ap)
	};

}

json::Value Strategy_KeepValue::exportState() const {
	return json::Object
			("p", p)
			("a", a);
}

IStrategy* Strategy_KeepValue::importState(json::Value src) const {
	return init(src["p"].getNumber(), src["a"].getNumber(),src["acm"].getNumber());
}

double Strategy_KeepValue::calcOrderSize(double price, double assets) const {
	double k = (a+cfg.ea) * p;
	double na = k / price;
	return (na - cfg.ea) - assets;
}

Strategy_KeepValue::MinMax Strategy_KeepValue::calcSafeRange(double assets,
		double currencies) const {
	double k = p*(a+cfg.ea);
	double n = p*std::exp(-currencies/k);
	double m = cfg.ea > 0?(k/cfg.ea):std::numeric_limits<double>::infinity();
	return MinMax {n,m};
}

double Strategy_KeepValue::getEquilibrium() const {
	return  p;
}

std::string_view Strategy_KeepValue::getID() const {
	return id;

}

IStrategy* Strategy_KeepValue::reset() const {
	return new Strategy_KeepValue(cfg);
}


IStrategy* Strategy_KeepValue::setMarketInfo(const IStockApi::MarketInfo &) const {
	return const_cast<Strategy_KeepValue *>(this);
}
