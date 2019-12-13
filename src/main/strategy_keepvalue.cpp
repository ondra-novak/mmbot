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

PStrategy Strategy_KeepValue::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double) const {
	if (isValid()) return this;
	else return new Strategy_KeepValue(cfg, ticker.last, assets);
}

std::pair<Strategy_KeepValue::OnTradeResult, PStrategy> Strategy_KeepValue::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		Strategy_KeepValue tmp(cfg, tradePrice, assetsLeft-tradeSize);
		return tmp.onTrade(minfo, tradePrice,tradeSize,assetsLeft, currencyLeft);
	}

	double p = this->p;

	double cf = (assetsLeft-tradeSize+cfg.ea)*(tradePrice - p);
	if (tradeSize == 0) p = tradePrice;
	double k = (a+cfg.ea) * p;
	double nv = k * std::log(tradePrice/p);
	double pf = cf - nv;
	double ap = (pf / tradePrice) * cfg.accum;
	double np = pf * (1.0 - cfg.accum);
	double new_a = (k / tradePrice) - cfg.ea;
	return {
		OnTradeResult{np,ap,0}, new Strategy_KeepValue(cfg, tradePrice, new_a+ap)
	};

}

json::Value Strategy_KeepValue::exportState() const {
	return json::Object
			("p", p)
			("a", a);
}

PStrategy Strategy_KeepValue::importState(json::Value src) const {
	return new Strategy_KeepValue(cfg, src["p"].getNumber(), src["a"].getNumber());
}

IStrategy::OrderData Strategy_KeepValue::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/) const {
	double k = (a+cfg.ea) * p;
	double na = k / price;
	return {
		0,
		calcOrderSize(a, assets, na - cfg.ea)
	};
}

Strategy_KeepValue::MinMax Strategy_KeepValue::calcSafeRange(
		const IStockApi::MarketInfo &,
		double assets,
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

PStrategy Strategy_KeepValue::reset() const {
	return new Strategy_KeepValue(cfg);
}

json::Value Strategy_KeepValue::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Assets", a)
				 ("Last price", p);

}


