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


Strategy_HalfHalf::Strategy_HalfHalf(double ea, double accu, double p, double a)
	:ea(ea), accu(accu), p(p), a(a+ea) {}



bool Strategy_HalfHalf::isValid() const {
	return a * p > 0;
}


IStrategy* Strategy_HalfHalf::init(double curPrice, double assets,
		double currency) const {
		return new Strategy_HalfHalf(ea, accu, curPrice, assets);
}

std::pair<Strategy_HalfHalf::OnTradeResult, IStrategy*> Strategy_HalfHalf::onTrade(
		double tradePrice, double size , double assetsLeft, double currencyLeft ) const {

	if (size == 0) {
		return std::make_pair(
				OnTradeResult{0,0},
				new Strategy_HalfHalf(ea, accu, tradePrice, assetsLeft));
	} else {

		double n = tradePrice;
		double na = a * sqrt(p/n);
		double v = a * p + a * n - 2 * a * sqrt(p *  n);
		double ap = (v / n) * accu;
		double np = v * (1-accu);
		return std::make_pair(
				OnTradeResult {np, ap},
				new Strategy_HalfHalf(ea,accu,n,na+ap-ea)
		);
	}
}

json::Value Strategy_HalfHalf::exportState() const {
	return json::Object
			("p",p)
			("a",a-ea);
}

IStrategy* Strategy_HalfHalf::importState(json::Value src) const {
	double new_p = src["p"].getNumber();
	double new_a = src["a"].getNumber();
	return new Strategy_HalfHalf(ea, accu, new_p, new_a);
}

double Strategy_HalfHalf::calcOrderSize(double n, double assets) const {
	double ca = assets + ea;
	return a * sqrt(p/n) - ca + (a * p + a * n - 2 * a * sqrt(p * n)) * accu / n;
}

Strategy_HalfHalf::MinMax Strategy_HalfHalf::calcSafeRange(double assets, double currencies) const {
	MinMax r;
	double s = a * p - currencies;
	r.max = ea<=0?std::numeric_limits<double>::infinity():p*pow2((assets + ea) / ea);
	r.min = s<=0?0:pow2(s/a)/p;
	return r;
}

double Strategy_HalfHalf::getEquilibrium() const {
	return p;
}

IStrategy* Strategy_HalfHalf::reset() const {
	return new Strategy_HalfHalf(ea, accu);
}

std::string_view Strategy_HalfHalf::getID() const {
	return id;
}

IStrategy* Strategy_HalfHalf::setMarketInfo(const IStockApi::MarketInfo &) const {
	return const_cast<Strategy_HalfHalf *>(this);
}
