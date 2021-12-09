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
	return st.base_pos > 0 && st.kmult > 0 && st.lastp > 0;
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


double Strategy_KeepValue2::calcInitP() const {
	double ofs = cfg.chngtm/st.kmult * (1.0/(30.0*24.0*60.0*60.0*1000.0))*(st.last_check_time - st.last_trade_time);
	return st.base_pos+ofs;
}

double Strategy_KeepValue2::calcCurr() const {
	return st.curr - (calcInitP() - st.base_pos)*st.kmult;
}

PStrategy Strategy_KeepValue2::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid()) {
		State nst = st;
		nst.last_check_time = ticker.time;
		return new Strategy_KeepValue2(cfg, std::move(nst));
	}
	else {
		State nst;
		nst.base_pos = assets;
		nst.kmult = ticker.last;
		nst.curr = cur;
		nst.lastp = ticker.last;
		nst.budget = calcBudget(cfg.ratio, nst.kmult, nst.lastp, nst.base_pos);
		nst.last_check_time = ticker.time;
		nst.last_trade_time = nst.last_check_time;
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

Strategy_KeepValue2::AccumInfo Strategy_KeepValue2::calcAccum(double new_price) const {
	double assets = st.pos;
	double init_p = calcInitP();
	double newk = calculateNewNeutral(assets, new_price);
	double newi = init_p*st.kmult/newk;
	double b2 = calcBudget(cfg.ratio, newk, new_price, newi);
	double pnl = (assets) * (new_price - st.lastp);
	double bdiff = b2 - st.budget;
	double extra = pnl - bdiff + (init_p - st.base_pos)*st.kmult;
	double accum = cfg.accum * (extra / new_price);
	double normp = (1.0-cfg.accum) * extra;
	if (cfg.reinvest) {
		normp = extra;
		newi += accum;
		accum = 0;
	}
	return {
		normp,
		accum,
		newk,
		newi
	};
}


std::pair<Strategy_KeepValue2::OnTradeResult, PStrategy> Strategy_KeepValue2::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return {{0,0},this};
	}
	auto n = calcAccum(tradePrice);


	auto cass = calcPosition(cfg.ratio, n.newk, tradePrice, n.newinit);
	auto diff = assetsLeft-cass-n.norma;
	auto budget = calcBudget(cfg.ratio, n.newk, tradePrice, n.newinit);

	return {
		{n.normp, n.norma,n.newk,0},
		PStrategy(new Strategy_KeepValue2(cfg, State {
			n.newinit, n.newk, tradePrice, calcCurr(), budget, assetsLeft-n.norma, diff * tradePrice, st.last_check_time, st.last_check_time
		}))
	};



}

json::Value Strategy_KeepValue2::exportState() const {
	return json::Object({
		{"init_pos",st.base_pos},
		{"budget",st.budget},
		{"curr",st.curr},
		{"berror",st.berror},
		{"kmult",st.kmult},
		{"lastp",st.lastp},
		{"pos",st.pos},
		{"last_check_time", st.last_check_time},
		{"last_trade_time", st.last_trade_time}

	});
}

PStrategy Strategy_KeepValue2::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst;
	newst.berror = src["berror"].getNumber();
	newst.budget = src["budget"].getNumber();
	newst.curr= src["curr"].getNumber();
	newst.base_pos= src["init_pos"].getNumber();
	newst.kmult = src["kmult"].getNumber();
	newst.lastp = src["lastp"].getNumber();
	newst.pos= src["pos"].getNumber();
	newst.last_check_time = src["last_check_time"].getUIntLong();
	newst.last_trade_time = src["last_check_time"].getUIntLong();
	return new Strategy_KeepValue2(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_KeepValue2::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double currency, bool ) const {

	auto n = calcAccum(price);
	double finPos = calcPosition(cfg.ratio, n.newk, price, n.newinit);
	finPos += n.norma;
	double diff = finPos - assets;
	return {0, diff};
}

Strategy_KeepValue2::MinMax Strategy_KeepValue2::calcSafeRange(
		const IStockApi::MarketInfo &,
		double assets,
		double currencies) const {

	double init_pos = calcInitP();
	double n = calcPriceFromCurrency(cfg.ratio, st.kmult, -currencies, init_pos);
	double m;
	double p = calcPosition(cfg.ratio, st.kmult, st.lastp, init_pos);
	if (assets < p*99.9) {
		m =calcEquilibrium(cfg.ratio, st.kmult, p-assets, init_pos);
	} else {
		m = std::numeric_limits<double>::infinity();
	}

	return MinMax {n,m};
}

double Strategy_KeepValue2::getEquilibrium(double assets) const {
	return calcEquilibrium(cfg.ratio, st.kmult, assets, calcInitP());
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

	double init_pos = calcInitP();
	return json::Object({
		{"Position", pos},
		{"Last price", pric},
		{"Currency", st.curr},
		{"Budget", calcBudget(cfg.ratio, st.kmult, st.lastp, init_pos)},
		{"Unprocessed volume", st.berror},
		{"Keep", st.kmult*init_pos}});

}
double Strategy_KeepValue2::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (minfo.leverage) return (currency/price)*0.5;
	else return (assets +currency/price)*0.5;
}



double Strategy_KeepValue2::calcCurrencyAllocation(double price) const {
	return calcCurrency(cfg.ratio, st.kmult, st.lastp, calcInitP())+st.curr;
}

Strategy_KeepValue2::ChartPoint Strategy_KeepValue2::calcChart(double price) const {
	double a = calcPosition(cfg.ratio, st.kmult, price, st.lastp);
	double b = calcBudget(cfg.ratio, st.kmult, price, st.lastp)+st.curr;
	return {true,a , b};
}


double Strategy_KeepValue2::calculateNewNeutral(double a, double price) const {
	if (!cfg.rebalance || (price-st.kmult)*(st.lastp - st.kmult) < 0) {
		return st.kmult;
	}
	double pnl = a*(price - st.lastp);
	double bc;
	double needb;
	double newk;
	double init_pos = calcInitP();
	if (price > st.kmult && price > st.lastp) {

			bc = calcBudget(cfg.ratio, st.kmult, price, init_pos);
			needb = bc-pnl;
			newk = numeric_search_r2(0.5*st.kmult, [&](double k){
				return calcBudget(cfg.ratio, k, st.lastp, st.kmult*init_pos/k) - needb;
			});
			if (newk<st.kmult) newk = st.kmult;
	} else if (price < st.kmult && price < st.lastp){
			bc = calcBudget(cfg.ratio, st.kmult, st.lastp, init_pos);
			needb = bc+pnl;
			newk = numeric_search_r1(1.5*st.kmult, [&](double k){
				return calcBudget(cfg.ratio, k, price, st.kmult*init_pos/k) - needb;
			});
	} else {
		newk = st.kmult;
	}
	if (newk < 1e-50 || newk > 1e+50) newk = st.kmult;
	return newk;

}
