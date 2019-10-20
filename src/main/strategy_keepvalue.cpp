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

Strategy_KeepValue::Strategy_KeepValue(const Config& cfg, double p, double a, double acm)
:cfg(cfg),p(p),a(a),acm(acm)
{
}

bool Strategy_KeepValue::isValid() const {
	return p*a >0;
}

IStrategy* Strategy_KeepValue::init(double curPrice, double assets, double) const {
	return new Strategy_KeepValue(cfg, curPrice, assets);
}

std::pair<Strategy_KeepValue::OnTradeResult, IStrategy*> Strategy_KeepValue::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	double new_a = a + calcOrderSizeRaw(tradePrice);
	double k = p * (a+cfg.ea);
	double cf = -tradeSize * tradePrice;
	double nv = k * std::log(tradePrice/p);
	double np = cf - nv;
	double ap = (np / tradePrice) * cfg.accum;
	np = np * (1.0 - cfg.accum);
	return std::pair (OnTradeResult{np,ap}, new Strategy_KeepValue(cfg, tradePrice, new_a, acm+ap));

}

json::Value Strategy_KeepValue::exportState() const {
	return json::Object
			("p", p)
			("a", a);
}

IStrategy* Strategy_KeepValue::importState(json::Value src) const {
	return init(src["p"].getNumber(), src["a"].getNumber(),0);
}

double Strategy_KeepValue::calcOrderSize(double price, double assets) const {
	return calcOrderSizeRaw(price) + (a - assets + acm);
}

Strategy_KeepValue::MinMax Strategy_KeepValue::calcSafeRange(double assets,
		double currencies) const {
	double k = p*a;
	double n = p*std::exp(-currencies/k);
	return MinMax {n,std::numeric_limits<double>::quiet_NaN()};
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

double Strategy_KeepValue::calcOrderSizeRaw(double price) const {
	double curVal = price * (a + cfg.ea);
	double k = p * (a + cfg.ea);
	double diff = k - curVal;
	double need = diff/price;
	logDebug("KeepValue: a=$5, p=$6, price=$7 curVal=$1, k=$2, diff=$3, need=$4", curVal, k, diff, need, a, p, price);
	return need;
}
