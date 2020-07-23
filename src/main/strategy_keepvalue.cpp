/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

using ondra_shared::logDebug;

#include "../shared/logOutput.h"
std::string_view Strategy_KeepValue::id = "keepvalue";

Strategy_KeepValue::Strategy_KeepValue(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_KeepValue::isValid() const {
	return st.valid && st.p > 0;
}

double Strategy_KeepValue::calcK(const State &st, const Config &cfg) {
	double k = st.p * (st.a + cfg.ea);
	double tm = (st.check_time - st.recalc_time);
	static double month_msec = 24.0*60.0*60.0*30.0*1000.0;
	double f = tm / month_msec;
	return std::max(k + cfg.chngtm * f,0.0);

}

double Strategy_KeepValue::calcK() const {
	return calcK(st,cfg);
}

PStrategy Strategy_KeepValue::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double) const {
	if (isValid()) {
		if (cfg.chngtm) {
			State nst = st;
			nst.check_time = ticker.time;
			if (!nst.recalc_time) nst.recalc_time = ticker.time;
			return new Strategy_KeepValue(cfg,std::move(nst));
		} else {
			return this;
		}
	}
	else {
		return new Strategy_KeepValue(cfg,
				State{true,ticker.last, assets,assets,ticker.time, ticker.time});
	}
}

double Strategy_KeepValue::calcAccountValue(const State &st, const Config &cfg, double price) {
	double k = calcK(st, cfg);
	double v = k * std::log(price/k) + 2*k;
	return v;
}

double Strategy_KeepValue::calcReqCurrency(const State &st, const Config &cfg, double price) {
	double k = calcK(st, cfg);
	double v = k * std::log(price/k) + k;
	return v;
}


double Strategy_KeepValue::calcAccumulation(const State &st, const Config &cfg, double price, double currencyLeft) {
	if (cfg.accum) {
		double k = calcK(st,cfg);
		double neutral = k/(st.n+cfg.ea);
		double autoaccum = cfg.keep_half?(price<neutral?0:cfg.accum):cfg.accum;

		double r1 = calcReqCurrency(st,cfg, st.p);
		double r2 = calcReqCurrency(st,cfg, price);
		double pl = -price*(calcA(st,cfg,price)-calcA(st,cfg,st.p));
		double nl = r2 - r1;
		double ex = pl -nl;
		double acc = (ex/price)*autoaccum;
		return acc;
	} else {
		return 0;
	}

}

double Strategy_KeepValue::calcA(const State &st, const Config &cfg, double price) {
	double k = calcK(st,cfg);
	return k/price;
}

Strategy_KeepValue::BudgetInfo Strategy_KeepValue::getBudgetInfo() const {
	return BudgetInfo{calcK(), st.a+cfg.ea};
}

double Strategy_KeepValue::calcNormalizedProfit(const State &st, const Config &cfg, double tradePrice, double tradeSize) {
	double cashflow = -tradePrice*tradeSize;
	double old_cash = calcReqCurrency(st,cfg, st.p);
	double new_cash = calcReqCurrency(st,cfg,tradePrice);
	double diff_cash = new_cash - old_cash;
	double np = cashflow - diff_cash;
	return np;
}

std::pair<Strategy_KeepValue::OnTradeResult, PStrategy> Strategy_KeepValue::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		Strategy_KeepValue tmp(cfg, State{true,tradePrice, assetsLeft-tradeSize,0,0});
		return tmp.onTrade(minfo, tradePrice,tradeSize,assetsLeft, currencyLeft);
	}
	double k = calcK();

	double accum = calcAccumulation(st,cfg,tradePrice, currencyLeft);
	double norm = calcNormalizedProfit(st,cfg,tradePrice,tradeSize);
	double neutral = k/(st.n+cfg.ea);


	State nst = st;
	nst.a = k/tradePrice + accum - cfg.ea;
	nst.recalc_time = st.check_time;
	nst.p = tradePrice;
	nst.check_time = st.check_time;
	nst.valid = true;

//	norm -= accum * tradePrice;

	return {
		OnTradeResult{norm,accum,neutral},
		new Strategy_KeepValue(cfg,  std::move(nst))
	};

}

json::Value Strategy_KeepValue::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a	)
			("n",st.n	)
			("valid",st.valid)
			("rt",st.recalc_time)
			("ct",st.check_time);
}

PStrategy Strategy_KeepValue::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst;
	newst.p = src["p"].getNumber();
	newst.a = src["a"].getNumber();
	newst.n = src["n"].getNumber();
	newst.recalc_time = src["rt"].getUIntLong();
	newst.check_time = src["ct"].getUIntLong();
	newst.valid = src["valid"].getBool();
	return new Strategy_KeepValue(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_KeepValue::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double currency) const {
	double k = calcK();
	double na = k / price + calcAccumulation(st,cfg,price, currency);
	return {
		0,
		calcOrderSize(st.a, assets, na - cfg.ea)
	};
}

Strategy_KeepValue::MinMax Strategy_KeepValue::calcSafeRange(
		const IStockApi::MarketInfo &,
		double assets,
		double currencies) const {
	double k = calcK();
	double n = st.p*std::exp(-currencies/k);
	double m = cfg.ea > 0?(k/cfg.ea):std::numeric_limits<double>::infinity();
	return MinMax {n,m};
}

double Strategy_KeepValue::getEquilibrium(double assets) const {
	//na = k / price
	//price = k / na
	return  calcK() / (assets+cfg.ea);
}

std::string_view Strategy_KeepValue::getID() const {
	return id;

}

PStrategy Strategy_KeepValue::reset() const {
	return new Strategy_KeepValue(cfg,{});
}

json::Value Strategy_KeepValue::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Assets", st.a)
				 ("Last price", st.p)
				 ("Keep", calcK());

}
double Strategy_KeepValue::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (minfo.leverage) return (currency/price)*0.5;
	else return (assets +cfg.ea+ currency/price)*0.5;
}


