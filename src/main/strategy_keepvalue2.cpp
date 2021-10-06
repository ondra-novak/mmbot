/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue2.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "numerical.h"
using ondra_shared::logDebug;

#include "../shared/logOutput.h"
std::string_view Strategy_KeepValue2::id = "keepvalue2";

Strategy_KeepValue2::Strategy_KeepValue2(const Config &cfg)
:cfg(cfg) {}

Strategy_KeepValue2::Strategy_KeepValue2(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_KeepValue2::isValid() const {
	return st.init_pos > 0 && st.kmult > 0 && st.lastp > 0;
}

double Strategy_KeepValue2::calcPosition(double ratio, double kmult, double price, double init_pos) {
	return init_pos*std::pow(kmult/price,ratio);
}
double Strategy_KeepValue2::calcBudget(double ratio, double kmult, double price, double init_pos) {
	if (ratio == 1) {
		return init_pos*kmult*(std::log(price/kmult)+1);
	} else {
		return init_pos*std::pow(kmult,ratio)*(std::pow(price,1.0-ratio) - std::pow(kmult, 1.0-ratio))/(1.0-ratio)+init_pos*kmult;
	}
}
double Strategy_KeepValue2::calcEquilibrium(double ratio, double kmult, double position, double init_pos) {
	return kmult*std::pow(init_pos/position, 1.0/ratio);
}
double Strategy_KeepValue2::calcPriceFromBudget(double ratio, double kmult, double budget, double init_pos) {
	if (ratio == 1) {
		return std::exp(1.0/(kmult * init_pos) - budget/(kmult * init_pos)) *  kmult;
	} else {
		return std::pow((std::pow(kmult,-ratio) * (budget - budget * ratio + kmult * init_pos * ratio))/init_pos,1.0/(1.0 - ratio));
	}

}
double Strategy_KeepValue2::calcCurrency(double ratio, double kmult, double price, double init_pos) {
	return calcBudget(ratio, kmult, price, init_pos) - calcPosition(ratio, kmult, price, init_pos)*price;
}
double Strategy_KeepValue2::calcPriceFromCurrency(double ratio, double kmult, double currency, double init_pos) {
	if (ratio == 1) {
		return std::exp(currency/(kmult *  init_pos)) * kmult;
	} else {
		if (currency < 0) {
			return numeric_search_r1(kmult, [&](double x){
				return calcCurrency(ratio, kmult, x, init_pos) - currency;
			});
		} else {
			return numeric_search_r2(kmult, [&](double x){
				return calcCurrency(ratio, kmult, x, init_pos) - currency;
			});

		}
	}

}


PStrategy Strategy_KeepValue2::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid()) {
		return this;
	}
	else {
		State nst;
		nst.init_pos = assets;
		nst.kmult = ticker.last;
		nst.curr = cur;
		nst.lastp = ticker.last;
		nst.budget = calcBudget(cfg.ratio, nst.kmult, nst.lastp, nst.init_pos);
		PStrategy s(new Strategy_KeepValue2(cfg, std::move(nst)));
		if (!s->isValid()) throw std::runtime_error("Unable to initialize strategy");
		return s;
	}
}

Strategy_KeepValue2::BudgetInfo Strategy_KeepValue2::getBudgetInfo() const {
	double a = calcPosition(cfg.ratio, st.kmult, st.lastp, st.lastp);
	double b = calcBudget(cfg.ratio, st.kmult, st.lastp, st.lastp)+st.curr;
	return {
		b,a
	};

}

std::pair<double,double> Strategy_KeepValue2::calcAccum(double new_price) const {
	double b2 = calcBudget(cfg.ratio, st.kmult, new_price, st.init_pos);
	double assets = st.pos;
	double pnl = (assets) * (new_price - st.lastp);
	double bdiff = b2 - st.budget;
	double extra = pnl - bdiff;
	double accum = cfg.accum * (extra / new_price);
	double normp = (1.0-cfg.accum) * extra;
	return {normp,accum};
}


std::pair<Strategy_KeepValue2::OnTradeResult, PStrategy> Strategy_KeepValue2::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return {{0,0},this};
	}
	auto normp = calcAccum(tradePrice);


	auto cass = calcPosition(cfg.ratio, st.kmult, tradePrice, st.init_pos);
	auto diff = assetsLeft-cass-normp.second;

	return {
		{normp.first, normp.second,0,0},
		PStrategy(new Strategy_KeepValue2(cfg, State {
			st.init_pos, st.kmult, tradePrice, st.curr, calcBudget(cfg.ratio, st.kmult, tradePrice, st.init_pos), assetsLeft-normp.second, diff * tradePrice
		}))
	};



}

json::Value Strategy_KeepValue2::exportState() const {
	return json::Object({
		{"init_pos",st.init_pos},
		{"budget",st.budget},
		{"curr",st.curr},
		{"berror",st.berror},
		{"kmult",st.kmult},
		{"lastp",st.lastp},
		{"pos",st.pos},
	});
}

PStrategy Strategy_KeepValue2::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst;
	newst.berror = src["berror"].getNumber();
	newst.budget = src["budget"].getNumber();
	newst.curr= src["curr"].getNumber();
	newst.init_pos= src["init_pos"].getNumber();
	newst.kmult = src["kmult"].getNumber();
	newst.lastp = src["lastp"].getNumber();
	newst.pos= src["pos"].getNumber();
	return new Strategy_KeepValue2(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_KeepValue2::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double currency, bool ) const {

	double finPos = calcPosition(cfg.ratio, st.kmult, price, st.init_pos);
	double accum = calcAccum(price).second;
	finPos += accum;
	double diff = finPos - assets;
	return {0, diff};
}

Strategy_KeepValue2::MinMax Strategy_KeepValue2::calcSafeRange(
		const IStockApi::MarketInfo &,
		double assets,
		double currencies) const {

	double n = calcPriceFromCurrency(cfg.ratio, st.kmult, -currencies, st.init_pos);
	double m;
	double p = calcPosition(cfg.ratio, st.kmult, st.lastp, st.init_pos);
	if (assets < p) {
		m =calcEquilibrium(cfg.ratio, st.kmult, p-assets, st.init_pos);
	} else {
		m = std::numeric_limits<double>::infinity();
	}

	return MinMax {n,m};
}

double Strategy_KeepValue2::getEquilibrium(double assets) const {
	return calcEquilibrium(cfg.ratio, st.kmult, assets, st.init_pos);
}

std::string_view Strategy_KeepValue2::getID() const {
	return id;
}

PStrategy Strategy_KeepValue2::reset() const {
	return new Strategy_KeepValue2(cfg,{});
}

json::Value Strategy_KeepValue2::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {
	double pos = st.pos;
	double pric = st.lastp;
	if (minfo.invert_price) {
		pos=-pos;
		pric = 1.0/pric;
	}

	return json::Object({
		{"Position", pos},
		{"Last price", pric},
		{"Currency", st.curr},
		{"Budget", calcBudget(cfg.ratio, st.kmult, st.lastp, st.init_pos)},
		{"Unprocessed volume", st.berror},
		{"Keep", st.kmult*st.init_pos}});

}
double Strategy_KeepValue2::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (minfo.leverage) return (currency/price)*0.5;
	else return (assets +currency/price)*0.5;
}



double Strategy_KeepValue2::calcCurrencyAllocation(double price) const {
	return calcBudget(cfg.ratio, st.kmult, st.lastp, st.init_pos)+st.curr;
}

Strategy_KeepValue2::ChartPoint Strategy_KeepValue2::calcChart(double price) const {
	double a = calcPosition(cfg.ratio, st.kmult, price, st.lastp);
	double b = calcBudget(cfg.ratio, st.kmult, price, st.lastp)+st.curr;
	return {true,a , b};
}
