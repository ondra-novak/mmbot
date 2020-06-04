/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_exponencial.h"

#include <chrono>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "../imtjson/src/imtjson/value.h"
using json::Value;
using ondra_shared::logDebug;

//This number is ration between argument k and price where value of assets and currency is balanced to 1:1
//p = to_balanced_factor * k
static constexpr double to_balanced_factor = 1.25643;

#include "../shared/logOutput.h"
std::string_view Strategy_Exponencial::id = "exponencial";

Strategy_Exponencial::Strategy_Exponencial(const Config &cfg):cfg(cfg) {

}

Strategy_Exponencial::Strategy_Exponencial(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_Exponencial::isValid() const {
	return st.valid && st.w > 0 && st.k > 0 && st.p > 0;
}


PStrategy Strategy_Exponencial::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid() || ticker.last<=0) return this;
	else {
		return new Strategy_Exponencial(init(cfg,ticker.last, assets,cur));
	}
}

Strategy_Exponencial Strategy_Exponencial::init(const Config &cfg, double price, double assets, double cur) {
	State nst;
	double a = assets + cfg.ea;
	double aa = cur/price;
	double min_a = aa * 0.1; //minimum allowed assets is 10% of available currency
	nst.valid = true;
	nst.k = price / to_balanced_factor;
	nst.p = price;
	nst.f = cur;
	if (a > min_a) { //initialize to 50:50 factor
		nst.w = a * std::exp(to_balanced_factor);
	} else {
		//force a to be 50:50 factor at current k
		double new_a = (a + aa)/2;
		//calculate w to achieve this factor - will trade for new a
		nst.w = new_a * std::exp(to_balanced_factor);
	}
	return Strategy_Exponencial(cfg, std::move(nst));
}

std::pair<Strategy_Exponencial::OnTradeResult, PStrategy> Strategy_Exponencial::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return init(cfg,tradePrice,assetsLeft,currencyLeft)
				.onTrade(minfo,tradePrice,tradeSize,assetsLeft,currencyLeft);
	}

	auto prof = calcNormalizedProfit(tradePrice, tradeSize);
	double new_a = prof.na + calcA(st, tradePrice);

	State nst = st;
	updateState(nst, new_a, tradePrice, currencyLeft);

	double neutral = nst.k * to_balanced_factor;

	return {
		OnTradeResult{prof.np,prof.na,neutral},
		new Strategy_Exponencial(cfg, std::move(nst))
	};
}

json::Value Strategy_Exponencial::exportState() const {
	return json::Object
			("p",st.p)
			("w",st.w)
			("k",st.k)
			("f",st.f)
			("ea", Value(cfg.ea).toString())
			("valid",st.valid);
}

PStrategy Strategy_Exponencial::importState(json::Value src) const {
	State newst {
		src["valid"].getBool(),
		src["w"].getNumber(),
		src["k"].getNumber(),
		src["p"].getNumber(),
		src["f"].getNumber()
	};
	if (Value(cfg.ea).toString() != src["ea"].toString()) {
		newst.valid = false;
	}
	return new Strategy_Exponencial(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_Exponencial::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/) const {

	double ordsz = calcOrderSize(calcA(st, st.p), assets+cfg.ea, calcA(st,price));
	return {0,ordsz};
}

Strategy_Exponencial::MinMax Strategy_Exponencial::calcSafeRange(
		const IStockApi::MarketInfo &,double , double currencies) const {
	double max = cfg.ea > 0? st.k * std::log(st.w/cfg.ea):std::numeric_limits<double>::infinity();
	double min = findRoot(st.w,st.k,st.p, currencies);
	return MinMax {min,max};
}

double Strategy_Exponencial::getEquilibrium(double assets) const {
	return  st.k*std::log(st.w/(assets+cfg.ea));
}

std::string_view Strategy_Exponencial::getID() const {
	return id;

}

PStrategy Strategy_Exponencial::reset() const {
	return new Strategy_Exponencial(cfg,{});
}

json::Value Strategy_Exponencial::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Assets/Position", (minfo.invert_price?-1:1)*calcA(st, st.p))
				 ("Last price ", minfo.invert_price?1.0/st.p:st.p)
				 ("Power (w)", st.w)
				 ("Anchor price (k)", minfo.invert_price?1.0/st.k:st.k)
				 ("Budget", calcAccountValue(st))
				 ("Budget Extra(+)/Debt(-)", minfo.leverage?Value():Value(st.f - calcReqCurrency(st,st.p)));

}

double Strategy_Exponencial::calcA(const State &st, double price) {
	return st.w * std::exp(-price/st.k);
}
void Strategy_Exponencial::updateState(State &st, double new_a, double new_p, double new_f) {
	double balanced = st.k * to_balanced_factor;
	if (new_p < balanced) {
		st.w = new_a * std::exp(new_p / st.k);
	} else {
		st.k = new_p / std::log(st.w / new_a);
	}
	st.p = new_p;
	st.f = new_f;
}

double Strategy_Exponencial::calcAccountValue(const State &st) {
	return st.k*st.w*(1 - std::exp(-st.p/st.k));
}

double Strategy_Exponencial::calcReqCurrency(const State &st, double price) {
	return st.w * (st.k - std::exp(-price/st.k) * (st.k + price));
}

Strategy_Exponencial::NormProfit Strategy_Exponencial::calcNormalizedProfit(double tradePrice, double tradeSize) const {
	double cashflow = -tradePrice*tradeSize;
	double old_cash = calcReqCurrency(st, st.p);
	double new_cash = calcReqCurrency(st, tradePrice);
	double diff_cash = new_cash - old_cash;
	double cp = cashflow - diff_cash;
	double na = cp/tradePrice*cfg.accum;
	double np = cp*(1-cfg.accum);
	return {np,na};
}

template<typename Fn>
static double numeric_search_r1(double start, double accuracy, Fn &&fn) {
	double min = 0;
	double max = start;
	double ref = fn(start);
	if (ref == 0) return start;
	double md = (min+max)/2;
	while (md > accuracy && (max - min) / md > accuracy) {
		double v = fn(md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return md;
}


double Strategy_Exponencial::findRoot(double w, double k, double p, double c) {
	auto base_fn = [=](double x) {
		return w * (k - std::exp(-x/k) * (k + x));
	};

	//calculate difference between ideal balance and current balance (<0)
	double diff = c - base_fn(p);
	//calculate maximum balance - difference must be below maximum
	double max = w * k;
	//if difference is positive, we have more money than need, so return 0
	if (diff >= 0) return 0;
	//if difference is negative and below maximum, result is infinity - cannot be calculated
	if (diff < -max) return std::numeric_limits<double>::infinity();

	//function to find root (root = equal zero)
	auto fn = [=](double x) {
		return base_fn(x) + diff;
	};

	//assume starting point at current price
	double start = p;
	//double starting point unless result is positive
	while (fn(start) < 0) start = start * 2;
	//search root numerically between zero and start point
	return numeric_search_r1(start, start*1e-6, std::move(fn));


}
