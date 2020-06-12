/*
 * strategy_halfhalf.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy_halfhalf.h"

#include <limits>

#include <imtjson/object.h>
#include "sgn.h"

std::string_view Strategy_HalfHalf::id = "halfhalf";


Strategy_HalfHalf::Strategy_HalfHalf(const Config &cfg, double p, double a)
	:cfg(cfg), p(p), a(a) {}



bool Strategy_HalfHalf::isValid() const {
	return (a+cfg.ea) * p > 0;
}


PStrategy Strategy_HalfHalf::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &curTicker, double assets, double currency) const {
	if (isValid()) return this;
	else return new Strategy_HalfHalf(cfg, curTicker.last, assets);
}

std::pair<Strategy_HalfHalf::OnTradeResult, PStrategy> Strategy_HalfHalf::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const {

	if (p == 0) {
		Strategy_HalfHalf tmp(cfg, tradePrice, assetsLeft-tradeSize);
		return tmp.onTrade(minfo, tradePrice,tradeSize,assetsLeft, currencyLeft);
	}


	double a = this->a + cfg.ea;
	double n = tradePrice;
	double p = this->p;
	//extra money earned by spread
	double v = a*p + a*n-2*a*sqrt(p* n);
	double na = a * sqrt(p/n);
	double ap = (v / n) * cfg.accum;
	double np = v * (1-cfg.accum);
	return std::make_pair(
			OnTradeResult {np, ap, 0},
			new Strategy_HalfHalf(cfg,n,na+ap-cfg.ea)
	);
}

json::Value Strategy_HalfHalf::exportState() const {
	return json::Object
			("p",p)
			("a",a);
}

PStrategy Strategy_HalfHalf::importState(json::Value src,const IStockApi::MarketInfo &) const {
	double new_p = src["p"].getNumber();
	double new_a = src["a"].getNumber();
	return new Strategy_HalfHalf(cfg, new_p, new_a);
}

IStrategy::OrderData Strategy_HalfHalf::getNewOrder(const IStockApi::MarketInfo &,
		double, double n, double dir, double assets,double currency) const {
	double a = this->a + cfg.ea;
	double p = this->p;
	double na = a * sqrt(p/n);
	return {0,calcOrderSize(this->a, assets, na-cfg.ea)};
}

Strategy_HalfHalf::MinMax Strategy_HalfHalf::calcSafeRange(const IStockApi::MarketInfo &,
		double assets, double currencies) const {
	MinMax r;
	double a = this->a + cfg.ea;
	double s = a * p - currencies;
	r.max = cfg.ea<=0?std::numeric_limits<double>::infinity():p*pow2((assets + cfg.ea) / cfg.ea);
	r.min = s<=0?0:pow2(s/a)/p;
	return r;
}

double Strategy_HalfHalf::getEquilibrium(double assets) const {
	return this->p * pow2(a/assets+cfg.ea);
}

PStrategy Strategy_HalfHalf::reset() const {
	return new Strategy_HalfHalf(cfg);
}

std::string_view Strategy_HalfHalf::getID() const {
	return id;
}

json::Value Strategy_HalfHalf::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Assets", a)
				 ("Last price", p)
				 ("Factor", (a+cfg.ea)*std::sqrt(p));

}

double Strategy_HalfHalf::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (minfo.leverage) return (currency/price)*0.5;
	else return (assets + cfg.ea+currency/price)*0.5;
}
