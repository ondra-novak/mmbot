/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_hyperbolic.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "sgn.h"
using ondra_shared::logDebug;

std::string_view Strategy_Hyperbolic::id = "hyperbolic";

Strategy_Hyperbolic::Strategy_Hyperbolic(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}
Strategy_Hyperbolic::Strategy_Hyperbolic(const Config &cfg)
:cfg(cfg) {}


bool Strategy_Hyperbolic::isValid() const {
	return st.neutral_price > 0;
}



Strategy_Hyperbolic Strategy_Hyperbolic::init(const Config &cfg, double price, double pos, double currency) {
	double mult = currency/price*cfg.power;
	double neutral = calcNeutral(mult, cfg.asym, pos, price);
	if (!std::isfinite(neutral) || neutral <= 0) neutral = price;
	return Strategy_Hyperbolic(cfg, State{neutral, price, pos, mult});
}

Strategy_Hyperbolic::PosCalcRes Strategy_Hyperbolic::calcPosition(double price) const {
	auto mm = calcRoots();
	bool lmt = false;
	if (price < mm.min) {price = mm.min; lmt = true;}
	if (price > mm.max) {price = mm.max; lmt = true;}

	double pos = ((st.neutral_price/price) -1 + cfg.asym) * st.mult;
	return {lmt,pos};
}

PStrategy Strategy_Hyperbolic::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double currency) const {
	if (isValid()) return this;
	else return new Strategy_Hyperbolic(init(cfg,ticker.last, assets, currency));
}

std::pair<Strategy_Hyperbolic::OnTradeResult, PStrategy> Strategy_Hyperbolic::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return init(cfg,tradePrice, assetsLeft, currencyLeft)
				.onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);
	}

	State nwst = st;
	auto cpos = calcPosition(tradePrice);
	if (cpos.limited) {
		nwst.neutral_price = calcNeutral(st.mult, cfg.asym, cpos.pos, tradePrice);
	} else {
		nwst.neutral_price = st.neutral_price + ((st.neutral_price * 199 + tradePrice)/200 - st.neutral_price) * cfg.reduction;
	}
	double val = calcPosValue(nwst.mult, cfg.asym, nwst.neutral_price, tradePrice);
	double profit = (assetsLeft - tradeSize) * (tradePrice - st.last_price);
	nwst.mult = (currencyLeft+val)/tradePrice * cfg.power;
	nwst.last_price = tradePrice;
	nwst.position = cpos.pos;
	double extra = (val - st.val) + profit;
	nwst.val = val;

	return {
		OnTradeResult{extra,0,st.neutral_price,0},
		new Strategy_Hyperbolic(cfg,  std::move(nwst))
	};

}

json::Value Strategy_Hyperbolic::exportState() const {
	return json::Object
			("neutral_price",st.neutral_price)
			("last_price",st.last_price)
			("position",st.position)
			("mult",st.mult)
			("val",st.val)
			("asym", static_cast<int>(cfg.asym * 1000)) ;

}

PStrategy Strategy_Hyperbolic::importState(json::Value src) const {
		State newst {
			src["neutral_price"].getNumber(),
			src["last_price"].getNumber(),
			src["position"].getNumber(),
			src["mult"].getNumber(),
			src["val"].getNumber()
		};
		if (src["asym"].getInt() != static_cast<int>(cfg.asym * 1000)) {
			newst.neutral_price = calcNeutral(newst.mult, cfg.asym, newst.position, newst.last_price);
		}
		return new Strategy_Hyperbolic(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_Hyperbolic::getNewOrder(
		const IStockApi::MarketInfo &,
		double curPrice, double price, double dir, double assets, double /*currency*/) const {
	auto cps = calcPosition(curPrice);
	if (cps.limited) {
		if (dir * assets > 0) return {0,0,Alert::stoploss};
		else if (dir * assets < 0) return {0,-assets,Alert::stoploss};
		else return {0,0,Alert::forced};
	} else {
		cps = calcPosition(price);
		double diff = calcOrderSize(st.position, assets, cps.pos);
		return {0, diff};
	}
}

Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	return calcRoots();
}

double Strategy_Hyperbolic::getEquilibrium() const {
	return  st.last_price;
}

std::string_view Strategy_Hyperbolic::getID() const {
	return id;

}

PStrategy Strategy_Hyperbolic::reset() const {
	return new Strategy_Hyperbolic(cfg,{});
}

json::Value Strategy_Hyperbolic::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Position", (minfo.invert_price?-1:1)*st.position)
				  ("Last price", minfo.invert_price?1/st.last_price:st.last_price)
				 ("Neutral price", minfo.invert_price?1/st.neutral_price:st.neutral_price)
				 ("Value", st.val)
				 ("Multiplier", st.mult);


}

double Strategy_Hyperbolic::calcPosValue(double power, double asym, double neutral, double curPrice) {
	return power * ((asym - 1) * (neutral - curPrice) + neutral * (log(neutral) - log(curPrice)));
}

template<typename Fn>
static double numerical_search(double min, double max, Fn &&fn, int steps) {
	double md = (min+max)/2;
	double ref = fn(max);
	while (steps) {
		double v = fn(md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
		--steps;
	}
	return md;
}

Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcRoots(double power,
		double asym, double neutral, double balance) {
	auto fncalc = [&](double x) {
		return calcPosValue(power,asym, neutral, x) - balance;
	};
	double r1 = numerical_search(0,neutral, fncalc,20);
	double rawmax = numerical_search(neutral*10,neutral*1e10, fncalc,20);
	double r2 = numerical_search(neutral,rawmax, fncalc,20);
	return {r1,r2};
}

double Strategy_Hyperbolic::calcMaxLoss() const {
	double lmt;
	if (cfg.max_loss == 0)
		lmt = st.mult / cfg.power * st.last_price / 2;
	else
		lmt = cfg.max_loss;

	return lmt;
}

Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcRoots() const {
	if (!rootsCache.has_value()) {
		double lmt = calcMaxLoss();
		rootsCache = calcRoots(st.mult, cfg.asym,st.neutral_price,lmt);
	}
	return *rootsCache;
}

double Strategy_Hyperbolic::adjNeutral(double price, double value) const {
	auto fncalc = [&](double x) {
		return calcPosValue(st.mult,cfg.asym, x, price) - value;
	};
	double dir = price - st.neutral_price;
	if (fncalc(st.neutral_price) < 0)
		return st.neutral_price;
	if (dir > 0) {
		return numerical_search(st.neutral_price, price*2, fncalc, 20);
	} else if (dir < 0) {
		return numerical_search(price/2, st.neutral_price, fncalc, 20);
	} else {
		return st.neutral_price;
	}
}

double Strategy_Hyperbolic::calcNeutral(double power, double asym,
		double position, double curPrice) {
	return (curPrice * (position + power - power *  asym))/power;
}
