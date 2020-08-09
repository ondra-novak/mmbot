/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_error_fn.h"

#include <chrono>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"
#include "sgn.h"
using json::Value;
using ondra_shared::logDebug;

static constexpr double to_balanced_factor = 0.95;
static const double sqrtpi = 1.7724538509055160272981674833411451827975494561223871282138077898;

#include "../shared/logOutput.h"
std::string_view Strategy_ErrorFn::id = "errorfn";

Strategy_ErrorFn::Strategy_ErrorFn(const Config &cfg):cfg(cfg) {

}

Strategy_ErrorFn::Strategy_ErrorFn(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_ErrorFn::isValid() const {
	return st.k > 0 && st.p > 0 && st.a + cfg.ea > 0;
}


PStrategy Strategy_ErrorFn::onIdle(
		const IStockApi::MarketInfo &m,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid()) return this;
	else {
		return init(m,ticker.last, assets,cur);
	}
}

PStrategy Strategy_ErrorFn::init(const IStockApi::MarketInfo &m, double price, double assets, double cur) const {
	if (price <= 0) throw std::runtime_error("Strategy: invalid ticker price");
	State nst;
	nst.k = price/ to_balanced_factor;
	if (st.p > 0 && st.a + cfg.ea > 0) {
		nst.a = st.a;
		nst.p = st.p;
	} else {
		if (cfg.ea + assets <= 0) {
			assets = calcInitialPosition(m,price,assets,cur);
			if (cfg.ea + assets <= 0) {
				 throw std::runtime_error("Strategy: Can't trade zero budget");
			}
		}
		nst.a = assets;
		nst.p = price;
	}
	nst.f = cur;
	return new Strategy_ErrorFn(cfg, std::move(nst));
}

Strategy_ErrorFn::State Strategy_ErrorFn::calcNewState(double new_a, double new_p, double new_f, double accum) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	Consts csts = calcRebalance(w,st.k,st.p,new_p,cfg.rebalance);
	double a = calcA(csts.w, csts.k, new_p) - cfg.ea;
	return State {a + accum,new_p,new_f,csts.k};

}

std::pair<Strategy_ErrorFn::OnTradeResult, PStrategy> Strategy_ErrorFn::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return init(minfo, tradePrice, assetsLeft, currencyLeft)
				->onTrade(minfo,tradePrice,tradeSize,assetsLeft,currencyLeft);
	}

	auto prof = calcNormalizedProfit(tradePrice, tradeSize);
	auto accum = calcAccumulation(st, cfg, tradePrice);
	auto new_a = calcA(tradePrice) + accum ;

	return {
		OnTradeResult{prof,accum,st.k*to_balanced_factor},
		new Strategy_ErrorFn(cfg, calcNewState(new_a, tradePrice, currencyLeft, accum))
	};
}

json::Value Strategy_ErrorFn::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a)
			("f",st.f)
			("k",st.k);
}

PStrategy Strategy_ErrorFn::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst {
		src["a"].getNumber(),
		src["p"].getNumber(),
		src["f"].getNumber(),
		src["k"].getNumber()
	};
	return new Strategy_ErrorFn(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_ErrorFn::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/) const {

	double newA = calcA(price);
	double extra = calcAccumulation(st, cfg, price);
	double ordsz = calcOrderSize(st.a, assets, newA+extra);
	return {0,ordsz};
}

Strategy_ErrorFn::MinMax Strategy_ErrorFn::calcSafeRange(
		const IStockApi::MarketInfo &,double , double currencies) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double max = cfg.ea > 0? calcEquilibrium(w,st.k, cfg.ea) :std::numeric_limits<double>::infinity();
	double min = findRoot(w,st.k,st.p, currencies);
	return MinMax {min,max};
}

double Strategy_ErrorFn::calcEquilibrium(double w,double k, double c){
	return std::sqrt(pow2(k)*std::log((2 *w )/(sqrtpi * c * k)));
}

double Strategy_ErrorFn::getEquilibrium(double assets) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double c = assets + cfg.ea;
	double k = st.k;
	return calcEquilibrium(w,  k, c);
}

std::string_view Strategy_ErrorFn::getID() const {
	return id;
}

PStrategy Strategy_ErrorFn::reset() const {
	return new Strategy_ErrorFn(cfg,{});
}

json::Value Strategy_ErrorFn::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	double w = calcW(st.a + cfg.ea, st.k, st.p);
	return json::Object("Assets/Position", (minfo.invert_price?-1:1)*st.a)
				 ("Last price ", minfo.invert_price?1.0/st.p:st.p)
				 ("Power (w)", w)
				 ("Anchor price (k)", minfo.invert_price?1.0/st.k:st.k)
				 ("Budget", calcAccountValue(st, cfg.ea, st.p))
				 ("Budget Extra(+)/Debt(-)", minfo.leverage?Value():Value(st.f - calcReqCurrency(st,cfg.ea,st.p, cfg.rebalance)));

}


double Strategy_ErrorFn::calcA(double w, double k, double p) {
	return 2*w*std::exp(-pow2(p/k))/(k * sqrtpi);
}

double Strategy_ErrorFn::calcA(double price) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	Consts csts = calcRebalance(w,st.k,st.p,price,cfg.rebalance);
	double a = calcA(csts.w, csts.k, price) - cfg.ea;
	return a;
}

double Strategy_ErrorFn::calcAccountValue(double w, double k, double p) {
	return w * std::erf(p/k);
}

double Strategy_ErrorFn::calcAccountValue(const State &st, double ea, double p) {
	double w = calcW(st.a + ea, st.k, st.p);
	double k = st.k;
	return calcAccountValue(w, k, p);
}

Strategy_ErrorFn::Consts Strategy_ErrorFn::calcRebalance(double w, double k, double p, double np, const Rebalance &rebalance) {
	double b = calcAccountValue(w,k,p);
	double v = calcA(w,k,p)*p;
	double ratio = v/b;
	if (ratio > rebalance.lo_p && ratio < rebalance.hi_p) {
		return Consts {w,k};
	} else {
		double new_k = k;
		if (ratio < rebalance.lo_p && np < p ) {
			new_k = k + (np/to_balanced_factor-k) * rebalance.lo_a;
		} else if (ratio > rebalance.hi_p && np > p) {
			new_k = k + (np/to_balanced_factor-k) * rebalance.hi_a;
		}

		double norm = calcAccountValue(1, new_k, p);
		double new_w = b/norm;
		return Consts {new_w, new_k};
	}

}

double Strategy_ErrorFn::calcReqCurrency(double w, double k, double price, double new_price, const Rebalance &rebalance) {
	Consts cst = calcRebalance(w,k,price,new_price,rebalance);
	double b = calcAccountValue(cst.w,cst.k,new_price);
	double v = calcA(cst.w,cst.k,new_price)*new_price;
	return b - v;
}
double Strategy_ErrorFn::calcReqCurrency(double w, double k, double price) {
	double b = calcAccountValue(w,k,price);
	double v = calcA(w,k,price)*price;
	return b - v;
}

double Strategy_ErrorFn::calcReqCurrency(const State &st, double ea, double price, const Rebalance &rebalance) {
	double w = calcW(st.a + ea, st.k, st.p);
	double k = st.k;
	return calcReqCurrency(w, k, st.p, price, rebalance);
}

double Strategy_ErrorFn::calcAccumulation(const State &st, const Config &cfg, double price) {
	if (cfg.accum) {
		double w = calcW(st.a + cfg.ea, st.k, st.p);
		double r1 = calcReqCurrency(st, cfg.ea, st.p, cfg.rebalance);
		double r2 = calcReqCurrency(st, cfg.ea, price, cfg.rebalance);
		double pl = -price*(calcA(w, st.k, price)-(st.a + cfg.ea));
		double nl = r2 - r1;
		double ex = pl -nl;
		double acc = (ex/price)*cfg.accum;
		return acc;
	} else {
		return 0;
	}

}


double Strategy_ErrorFn::calcNormalizedProfit(double tradePrice, double tradeSize) const {
	double cashflow = -tradePrice*tradeSize;
	double old_cash = calcReqCurrency(st,cfg.ea, st.p, cfg.rebalance);
	double new_cash = calcReqCurrency(st, cfg.ea, tradePrice, cfg.rebalance);
	double diff_cash = new_cash - old_cash;
	double np = cashflow - diff_cash;

	return np;
}
Strategy_ErrorFn::BudgetInfo Strategy_ErrorFn::getBudgetInfo() const {
	return BudgetInfo {
		calcAccountValue(st,cfg.ea,st.p),
		st.a + cfg.ea
	};
}

double Strategy_ErrorFn::calcW(double a, double k, double p) {
	return a/calcA(1, k, p);
}

double Strategy_ErrorFn::findRoot(double w, double k, double p, double c) {
	auto base_fn = [=](double x) {
		return calcReqCurrency(w, k, x) ;
	};

	//calculate difference between ideal balance and current balance (<0)
	double diff = c - base_fn(p);
	//if difference is positive, we have more money than need, so return 0
	if (diff >= 0) return 0;

	//function to find root (root = equal zero)
	auto fn = [=](double x) {
		return base_fn(x) + diff;
	};

	double pp = fn(p);
	double r;
	if (pp > 0) {
		r = numeric_search_r1(p, std::move(fn));
	} else if (pp < 0) {
		r = numeric_search_r2(p, std::move(fn));
	} else {
		r = p;
	}

	return r;
}

double Strategy_ErrorFn::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(assets+cfg.ea)*price+currency;
	double k = price / to_balanced_factor;
	double norm_val = calcAccountValue(1, k, price);
	double w = budget / norm_val;
	double a= calcA(w,k,price)-cfg.ea;
	return a;
}

std::optional<IStrategy::BudgetExtraInfo> Strategy_ErrorFn::getBudgetExtraInfo(double price, double currency) const {
	double b = calcAccountValue(st, cfg.ea, price);
	double e = (st.a+cfg.ea) * price + currency - b;
	return BudgetExtraInfo {b, e};
}

