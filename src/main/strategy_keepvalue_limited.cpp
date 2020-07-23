/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue_limited.h"

#include <chrono>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"
using json::Value;
using ondra_shared::logDebug;

static constexpr double to_balanced_factor = 2.875803333;

#include "../shared/logOutput.h"
std::string_view Strategy_KVLimited::id = "kvlimited";

Strategy_KVLimited::Strategy_KVLimited(const Config &cfg):cfg(cfg) {

}

Strategy_KVLimited::Strategy_KVLimited(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_KVLimited::isValid() const {
	return st.k > 0 && st.p > 0 && st.a + cfg.ea > 0;
}


PStrategy Strategy_KVLimited::onIdle(
		const IStockApi::MarketInfo &m,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid()) return this;
	else {
		return init(m,ticker.last, assets,cur);
	}
}

PStrategy Strategy_KVLimited::init(const IStockApi::MarketInfo &m, double price, double assets, double cur) const {
	if (price <= 0) throw std::runtime_error("Strategy: invalid ticker price");
	if (cfg.optp <=0) throw std::runtime_error("Strategy: Incomplete configuration");
	State nst;
	nst.k = m.invert_price?(1.0 / cfg.optp):cfg.optp / to_balanced_factor;
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
	return new Strategy_KVLimited(cfg, std::move(nst));
}

std::pair<Strategy_KVLimited::OnTradeResult, PStrategy> Strategy_KVLimited::onTrade(
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

	State nst = st;
	nst.a = new_a;
	nst.p = tradePrice;
	nst.f = currencyLeft;
	return {
		OnTradeResult{prof,accum},
		new Strategy_KVLimited(cfg, std::move(nst))
	};
}

json::Value Strategy_KVLimited::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a)
			("f",st.f);
}

PStrategy Strategy_KVLimited::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst {
		src["a"].getNumber(),
		src["p"].getNumber(),
		src["f"].getNumber()
	};
	return new Strategy_KVLimited(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_KVLimited::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/) const {

	double newA = calcA(price);
	double extra = calcAccumulation(st, cfg, price);
	double ordsz = calcOrderSize(st.a, assets, newA+extra);
	return {0,ordsz};
}

Strategy_KVLimited::MinMax Strategy_KVLimited::calcSafeRange(
		const IStockApi::MarketInfo &,double , double currencies) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double max = cfg.ea > 0? w/cfg.ea - st.k:std::numeric_limits<double>::infinity();
	double min = findRoot(w,st.k,st.p, currencies);
	return MinMax {min,max};
}

double Strategy_KVLimited::getEquilibrium(double assets) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	return   w/(assets + cfg.ea) - st.k;
}

std::string_view Strategy_KVLimited::getID() const {
	return id;

}

PStrategy Strategy_KVLimited::reset() const {
	return new Strategy_KVLimited(cfg,{});
}

json::Value Strategy_KVLimited::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	double w = calcW(st.a + cfg.ea, st.k, st.p);
	return json::Object("Assets/Position", (minfo.invert_price?-1:1)*st.a)
				 ("Last price ", minfo.invert_price?1.0/st.p:st.p)
				 ("Power (w)", w)
				 ("Anchor price (k)", minfo.invert_price?1.0/st.k:st.k)
				 ("Budget", calcAccountValue(st, cfg.ea, st.p))
				 ("Budget Extra(+)/Debt(-)", minfo.leverage?Value():Value(st.f - calcReqCurrency(st,cfg.ea,st.p)));

}

double Strategy_KVLimited::calcA(double w, double k, double p) {
	return w /(p+k);
}

double Strategy_KVLimited::calcA(double price) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double a = calcA(w, st.k, price) - cfg.ea;
	return a;
}

double Strategy_KVLimited::calcAccountValue(double w, double k, double p) {
	return w * std::log((k + p) / k);
}

double Strategy_KVLimited::calcAccountValue(const State &st, double ea, double p) {
	double w = calcW(st.a + ea, st.k, st.p);
	double k = st.k;
	return calcAccountValue(w, k, p);
}

double Strategy_KVLimited::calcReqCurrency(double w, double k, double price) {
	return w *( std::log((k + price) / k) - price  / (price + k) );
}

double Strategy_KVLimited::calcReqCurrency(const State &st, double ea, double price) {
	double w = calcW(st.a + ea, st.k, st.p);
	double k = st.k;
	return calcReqCurrency(w, k, price);
}

double Strategy_KVLimited::calcAccumulation(const State &st, const Config &cfg, double price) {
	if (cfg.accum) {
		double w = calcW(st.a + cfg.ea, st.k, st.p);
		double r1 = calcReqCurrency(st, cfg.ea, st.p);
		double r2 = calcReqCurrency(st, cfg.ea, price);
		double pl = -price*(calcA(w, st.k, price)-(st.a + cfg.ea));
		double nl = r2 - r1;
		double ex = pl -nl;
		double acc = (ex/price)*cfg.accum;
		return acc;
	} else {
		return 0;
	}

}


double Strategy_KVLimited::calcNormalizedProfit(double tradePrice, double tradeSize) const {
	double cashflow = -tradePrice*tradeSize;
	double old_cash = calcReqCurrency(st,cfg.ea, st.p);
	double new_cash = calcReqCurrency(st, cfg.ea, tradePrice);
	double diff_cash = new_cash - old_cash;
	double np = cashflow - diff_cash;

	return np;
}
Strategy_KVLimited::BudgetInfo Strategy_KVLimited::getBudgetInfo() const {
	return BudgetInfo {
		calcAccountValue(st,cfg.ea,st.p),
		st.a + cfg.ea
	};
}

double Strategy_KVLimited::calcW(double a, double k, double p) {
	return a * (k + p);
}

double Strategy_KVLimited::findRoot(double w, double k, double p, double c) {
	auto base_fn = [=](double x) {
		return calcReqCurrency(w, k, x) - c;
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

double Strategy_KVLimited::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(assets+cfg.ea)*price+currency;
	double k = minfo.invert_price?1.0/cfg.optp:cfg.optp / to_balanced_factor;
	double norm_val = calcAccountValue(1, k, price);
	double w = budget / norm_val;
	double a= calcA(w,k,price)-cfg.ea;
	return a;
}
