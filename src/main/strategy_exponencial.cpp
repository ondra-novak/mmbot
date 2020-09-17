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
#include "numerical.h"
using json::Value;
using ondra_shared::logDebug;

static constexpr double to_balanced_factor = 1.581976707;

#include "../shared/logOutput.h"
std::string_view Strategy_Exponencial::id = "exponencial";

Strategy_Exponencial::Strategy_Exponencial(const Config &cfg):cfg(cfg) {

}

Strategy_Exponencial::Strategy_Exponencial(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_Exponencial::isValid() const {
	return st.k > 0 && st.p > 0 && st.a + cfg.ea > 0;
}


PStrategy Strategy_Exponencial::onIdle(
		const IStockApi::MarketInfo &m,
		const IStockApi::Ticker &ticker, double assets, double cur) const {
	if (isValid()) return this;
	else {
		return init(m,ticker.last, assets,cur);
	}
}

PStrategy Strategy_Exponencial::init(const IStockApi::MarketInfo &m, double price, double assets, double cur) const {
	if (price <= 0) throw std::runtime_error("Strategy: invalid ticker price");
	if (cfg.optp <=0) throw std::runtime_error("Strategy: Incomplete configuration");
	State nst;
	nst.k = (m.invert_price?(1.0 / cfg.optp):cfg.optp) / to_balanced_factor;
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
	return new Strategy_Exponencial(cfg, std::move(nst));
}

std::pair<Strategy_Exponencial::OnTradeResult, PStrategy> Strategy_Exponencial::onTrade(
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
		new Strategy_Exponencial(cfg, std::move(nst))
	};
}

json::Value Strategy_Exponencial::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a)
			("f",st.f);
}

PStrategy Strategy_Exponencial::importState(json::Value src,const IStockApi::MarketInfo &) const {
	State newst {
		src["a"].getNumber(),
		src["p"].getNumber(),
		src["f"].getNumber()
	};
	return new Strategy_Exponencial(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_Exponencial::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/, bool /*rej*/) const {

	double newA = calcA(price);
	double extra = calcAccumulation(st, cfg, price);
	double ordsz = calcOrderSize(st.a, assets, newA+extra);
	return {0,ordsz};
}

Strategy_Exponencial::MinMax Strategy_Exponencial::calcSafeRange(
		const IStockApi::MarketInfo &,double , double currencies) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double max = cfg.ea > 0? st.k * std::log(w/cfg.ea):std::numeric_limits<double>::infinity();
	double min = findRoot(w,st.k,st.p, currencies);
	return MinMax {min,max};
}

double Strategy_Exponencial::getEquilibrium(double assets) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	return  st.k*std::log(w/(assets+cfg.ea));
}

std::string_view Strategy_Exponencial::getID() const {
	return id;

}

PStrategy Strategy_Exponencial::reset() const {
	return new Strategy_Exponencial(cfg,{});
}

json::Value Strategy_Exponencial::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	double w = calcW(st.a + cfg.ea, st.k, st.p);
	return json::Object("Assets/Position", (minfo.invert_price?-1:1)*st.a)
				 ("Last price ", minfo.invert_price?1.0/st.p:st.p)
				 ("Power (w)", w)
				 ("Anchor price (k)", minfo.invert_price?1.0/st.k:st.k)
				 ("Budget", calcAccountValue(st, cfg.ea, st.p))
				 ("Budget Extra(+)/Debt(-)", minfo.leverage?Value():Value(st.f - calcReqCurrency(st,cfg.ea,st.p)));

}

double Strategy_Exponencial::calcA(double w, double k, double p) {
	return w * std::exp(-p/k);
}

double Strategy_Exponencial::calcA(double price) const {
	double w = calcW(st.a + cfg.ea, st.k, st.p);
	double a = calcA(w, st.k, price) - cfg.ea;
	return a;
}

double Strategy_Exponencial::calcAccountValue(const State &st, double ea, double p) {
	double w = calcW(st.a + ea, st.k, st.p);
	return st.k*w*(1 - std::exp(-p/st.k));
}


double Strategy_Exponencial::calcReqCurrency(const State &st, double ea, double price) {
	double w = calcW(st.a + ea, st.k, st.p);
	return w * (st.k - std::exp(-price/st.k) * (st.k + price));
}

double Strategy_Exponencial::calcAccumulation(const State &st, const Config &cfg, double price) {
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


double Strategy_Exponencial::calcNormalizedProfit(double tradePrice, double tradeSize) const {
	double cashflow = -tradePrice*tradeSize;
	double old_cash = calcReqCurrency(st,cfg.ea, st.p);
	double new_cash = calcReqCurrency(st, cfg.ea, tradePrice);
	double diff_cash = new_cash - old_cash;
	double np = cashflow - diff_cash;

	return np;
}
Strategy_Exponencial::BudgetInfo Strategy_Exponencial::getBudgetInfo() const {
	return BudgetInfo {
		calcAccountValue(st,cfg.ea,st.p),
		st.a + cfg.ea
	};
}

double Strategy_Exponencial::calcW(double a, double k, double p) {
	return a * std::exp(p/k);
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

double Strategy_Exponencial::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(assets+cfg.ea)*price+currency;
	double k = (minfo.invert_price?1.0/cfg.optp:cfg.optp) / to_balanced_factor;
	double norm_val = k*(1 - std::exp(-price/k));
	double w = budget / norm_val;
	double a= calcA(w,k,price)-cfg.ea;
	return a;
}

std::optional<IStrategy::BudgetExtraInfo> Strategy_Exponencial::getBudgetExtraInfo(double price, double currency) const {
	double b = calcAccountValue(st,cfg.ea,price);
	double e = (st.a+cfg.ea) * price + currency - b ;
	return BudgetExtraInfo {b, e};
}

