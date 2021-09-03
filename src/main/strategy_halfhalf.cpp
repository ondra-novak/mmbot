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
	return p > 0;
}


PStrategy Strategy_HalfHalf::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &curTicker, double assets, double currency) const {
	if (isValid()) return this;
	else {
		if (assets+cfg.ea <=0) {
			assets = currency/(2*curTicker.last) - cfg.ea;
		}
		return new Strategy_HalfHalf(cfg, curTicker.last, assets);
	}
}

double Strategy_HalfHalf::calcAccumulation(double n) const {
	double a = this->a + cfg.ea;
	double old_b = a * p;
	double new_b = a * std::sqrt(p*n);
	double b_diff = new_b - old_b;
	double cashflow = (a - calcNewA(n)) * n;
	double np = cashflow - b_diff;
	double ap = np / n;
	return ap * cfg.accum;

}

double Strategy_HalfHalf::calcNormProfit(double s, double n) const {
	double a = this->a + cfg.ea;
	double old_b = a * p;
	double new_b = a * std::sqrt(p*n);
	double b_diff = new_b - old_b;
	double cashflow = -s * n;
	double np = cashflow - b_diff;
	return np;
}

Strategy_HalfHalf::BudgetInfo Strategy_HalfHalf::getBudgetInfo() const {
	double a= cfg.ea + this->a;
	return BudgetInfo {2*a*p,a};
}

double Strategy_HalfHalf::calcNewA(double n) const {
	double new_a = (a+cfg.ea) * std::sqrt(p/n);
	return new_a;
}

std::pair<Strategy_HalfHalf::OnTradeResult, PStrategy> Strategy_HalfHalf::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const {

	if (!isValid()) {
		Strategy_HalfHalf tmp(cfg, tradePrice, assetsLeft-tradeSize);
		return tmp.onTrade(minfo, tradePrice,tradeSize,assetsLeft, currencyLeft);
	}


	double ap = calcAccumulation(tradePrice);
	double np = tradeSize == assetsLeft?0:calcNormProfit(tradeSize, tradePrice);
	double new_a = calcNewA(tradePrice)+ ap - cfg.ea;

	return std::make_pair(
			OnTradeResult {np, ap, getEquilibrium(assetsLeft)},
			new Strategy_HalfHalf(cfg,tradePrice,new_a)
	);
}

json::Value Strategy_HalfHalf::exportState() const {
	return json::Object({
		{"p",p},
		{"a",a}
	});
}

PStrategy Strategy_HalfHalf::importState(json::Value src,const IStockApi::MarketInfo &) const {
	double new_p = src["p"].getNumber();
	double new_a = src["a"].getNumber();
	return new Strategy_HalfHalf(cfg, new_p, new_a);
}

IStrategy::OrderData Strategy_HalfHalf::getNewOrder(const IStockApi::MarketInfo &,
		double, double n, double dir, double assets,double currency, bool ) const {
	double new_a = calcNewA(n) + calcAccumulation(n);
	return {0,calcOrderSize(this->a, assets, new_a-cfg.ea)};
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
	return this->p * pow2((a+cfg.ea)/(assets+cfg.ea));
}

PStrategy Strategy_HalfHalf::reset() const {
	return new Strategy_HalfHalf(cfg);
}

std::string_view Strategy_HalfHalf::getID() const {
	return id;
}

json::Value Strategy_HalfHalf::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	double a= cfg.ea + this->a;

	return json::Object({
		{"Assets", a},
		{"Last price", p},
		{"Budget", 2*a*p},
		{"Factor", a*std::sqrt(p)}
	});

}

double Strategy_HalfHalf::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (minfo.leverage) return (currency/price)*0.5;
	else return (assets + currency/price)*0.5;
}


double Strategy_HalfHalf::calcCurrencyAllocation(double price) const {
	return price*a;
}

Strategy_HalfHalf::ChartPoint Strategy_HalfHalf::calcChart(double price) const {
	return {true,calcNewA(price),2*a*std::sqrt(p*price)};
}
