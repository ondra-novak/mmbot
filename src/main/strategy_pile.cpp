/*
 * strategy_pile.cpp
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#include <imtjson/object.h>
#include "strategy_pile.h"

#include <cmath>

const double Strategy_Pile::ln2 = std::log(2);

Strategy_Pile::Strategy_Pile(const Config &cfg):cfg(cfg) {}
Strategy_Pile::Strategy_Pile(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Pile::id = "pile";

PStrategy Strategy_Pile::init(double price, double assets, double currency) const {

    State nst = {};
    nst.budget = (leveraged?0:(price*assets))+currency;
    nst.kmult = calcKMult(price, nst.budget, cfg.ratio);
    nst.lastp = price;

    if (cfg.isBoostEnabled()) {
        double pos = assets - calcPosition(cfg.ratio, st.kmult, price);
        double bpw = cfg.boost_power * nst.budget;
        nst.boost_neutral_price = calcBoostNeutralFromPos(pos, price, bpw, cfg.boost_volatility);
        nst.boost_last_price = price;
        nst.boost_value = calcBoostValue(price, nst.boost_neutral_price, bpw, cfg.boost_volatility);
    }



    PStrategy out(new Strategy_Pile(cfg, std::move(nst)));



    if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
    return out;

}

double Strategy_Pile::calcKMult(double price, double budget, double ratio) {
    double c = calcBudget(ratio, 1, price);
    double kmult = budget/c;
    return kmult;
}

double Strategy_Pile::calcPosition(double ratio, double kmult, double price) {
	return kmult*std::pow(price, ratio-1);
}

double Strategy_Pile::calcBudget(double ratio, double kmult, double price) {
	return kmult*std::pow(price,ratio)/(ratio);
}

double Strategy_Pile::calcEquilibrium(double ratio, double kmul, double position) {
	return std::pow<double>(position/kmul,-1.0/(1-ratio));
}

double Strategy_Pile::calcPriceFromBudget(double ratio, double kmul, double budget) {
	return std::pow(budget*ratio/kmul, 1.0/(ratio));
}

double Strategy_Pile::calcCurrency(double ratio, double kmult, double price) {
	return kmult*(std::pow(price,ratio)/(ratio) - std::pow(price, ratio-1)*price);
}

double Strategy_Pile::calcPriceFromCurrency(double ratio, double kmult, double currency) {
	return std::pow(-(kmult - kmult/ratio)/currency,-1.0/ratio);
}



IStrategy::OrderData Strategy_Pile::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double finPos = calcPosition(cfg.ratio, st.kmult, new_price);
	double finPl = (new_price - st.lastp) * assets;
    double finBudget = calcBudget(cfg.ratio, st.kmult, new_price);

	if (cfg.isBoostEnabled()) {
	    double bpw = st.budget * cfg.boost_power;
	    double pos = assets - calcPosition(cfg.ratio, st.kmult, st.boost_last_price);
	    double bpl = (new_price - st.boost_last_price) * pos;
	    double v = bpl + st.boost_value;
	    double newk = v>=0?st.boost_neutral_price:calcBoostNeutralFromValue(pos, v, new_price, bpw, cfg.boost_volatility);
	    double npos = calcBoostPosition(new_price, newk, bpw, cfg.boost_volatility);
	    finPos += npos;
	    finBudget += v;
	}

	double np = finPl -  (finBudget - st.budget);
	double accum = cfg.accum * np/new_price;
	finPos += accum;
	double diff = finPos - assets;
	return {0, diff};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Pile::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	if (!isValid()) return
			this->init(tradePrice, assetsLeft-tradeSize, currencyLeft, minfo.leverage != 0)
				 ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double np;
	State nst(st);


	if (cfg.isBoostEnabled()) {

	    double hhpp = calcPosition(cfg.ratio, st.kmult, st.lastp);
	    double hhplch = (tradePrice - st.lastp) * hhpp;
	    nst.budget = calcBudget(cfg.ratio, st.kmult, tradePrice);
        double budgetchange = nst.budget - st.budget;;
        np = hhplch - budgetchange;

        double hhpos = calcPosition(cfg.ratio, st.kmult, tradePrice);
        double pos = assetsLeft - hhpos;
        double bpw_old = cfg.boost_power * st.budget;
        double bpw = cfg.boost_power * nst.budget;
        double bbpp = calcBoostPosition(st.boost_last_price, st.boost_neutral_price, bpw_old, cfg.boost_volatility);
        double eq = calcBoostPriceFromPos(pos, st.boost_neutral_price, bpw_old, cfg.boost_volatility);
        double bbplch = (eq - st.boost_last_price) * bbpp;
        double newv = st.boost_value+bbplch;
        double newk = calcBoostNeutralFromValue(pos, newv, eq, bpw, cfg.boost_volatility);
        nst.boost_value = calcBoostValue(eq, newk, bpw, cfg.boost_volatility);
        nst.boost_neutral_price = newk;
        nst.boost_last_price = eq;
        np += bbplch - (nst.boost_value - st.boost_value);

	} else {
	    double prevpos = assetsLeft - tradeSize;
	    double plchange = (tradePrice - st.lastp) * prevpos;
	    nst.budget = calcBudget(cfg.ratio, st.kmult, tradePrice);
	    double budgetchange = nst.budget - st.budget;;
	    np = plchange - budgetchange;
	    nst.boost_neutral_price = 0;
	    nst.boost_value = 0;
	    nst.boost_last_price = 0;
	}
	double accum = np/tradePrice * cfg.accum;
	np = np * (1-cfg.accum);

	return {
		{np, accum,nst.boost_neutral_price,0},
		PStrategy(new Strategy_Pile(cfg, std::move(nst)))
	};

}

PStrategy Strategy_Pile::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State nst = {};
    nst.budget = src["budget"].getNumber();
    nst.lastp = src["lastp"].getNumber();
    nst.kmult = calcKMult(nst.lastp, nst.budget, cfg.ratio);
    if (cfg.isBoostEnabled()) {
        auto boost = src["boost"];
        nst.boost_last_price = boost["p"].getNumber();
        nst.boost_neutral_price = boost["k"].getNumber();
        nst.boost_value = boost["v"].getNumber();
    }
    return new Strategy_Pile(cfg, std::move(nst));
}

IStrategy::MinMax Strategy_Pile::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	double  pos = calcPosition(cfg.ratio, st.kmult, st.lastp);
	MinMax r;
	if (pos > assets) {
		r.max = calcEquilibrium(cfg.ratio, st.kmult, pos - assets);
	} else {
		r.max = std::numeric_limits<double>::infinity();
	}
	double cur = calcCurrency(cfg.ratio, st.kmult, st.lastp);
	double avail = currencies  + (assets>pos?(assets-pos)*st.lastp:0);
	if (cur > avail) {
		r.min = calcPriceFromCurrency(cfg.ratio, st.kmult, cur-avail);
	} else {
		r.min = 0;
	}
	return r;
}

bool Strategy_Pile::isValid() const {
	return st.budget > 0 && st.kmult > 0 && st.lastp > 0 && (!cfg.isBoostEnabled() || (st.boost_last_price > 0 && st.boost_neutral_price > 0));
}

json::Value Strategy_Pile::exportState() const {
	return json::Object {
		{"lastp",st.lastp},
		{"budget",st.budget},
		{"boost", std::Object {
		    {"k", st.boost_neutral_price},
		    {"p", st.boost_last_price},
		    {"v", st.boost_value}
		}}

	};
}

std::string_view Strategy_Pile::getID() const {
	return id;
}

double Strategy_Pile::getCenterPrice(double lastPrice, double assets) const {
	return getEquilibrium(assets);
}

double Strategy_Pile::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(currency+price*assets);
	return (budget * cfg.ratio)/price;
}

IStrategy::BudgetInfo Strategy_Pile::getBudgetInfo() const {
	return {
		calcBudget(cfg.ratio, st.kmult, st.lastp),
		calcPosition(cfg.ratio, st.kmult, st.lastp)
	};
}

double Strategy_Pile::getEquilibrium(double assets) const {
	return calcEquilibrium(cfg.ratio, st.kmult, assets);
}

double Strategy_Pile::calcEquityAllocation(double price) const {
    double eq1 = calcBudget(cfg.ratio, st.kmult, price);
    double eq2 = cfg.isBoostEnabled()?calcBoostValue(price, st.boost_neutral_price, cfg.boost_power * st.budget, cfg.boost_volatility):0;
    double eq = eq1 + eq2;
    if (leveraged) return eq;
    double pos = calcPosition(cfg.r, kmult, price)
	return
}

IStrategy::ChartPoint Strategy_Pile::calcChart(double price) const {
	return ChartPoint{
		true,
		calcPosition(cfg.ratio, st.kmult, price),
		calcBudget(cfg.ratio, st.kmult, price)
	};
}

PStrategy Strategy_Pile::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Pile::reset() const {
	return new Strategy_Pile(cfg);
}

json::Value Strategy_Pile::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object{
		{"Budget",st.budget},
		{"Multiplier",st.kmult},
	};
}

double Strategy_Pile::calcBoostPosition(double price, double neutral_price, double bpw, double bvl) {
    return bpw*std::sinh(ln2*(1-price/neutral_price)/bvl);
}

double Strategy_Pile::calcBoostValue(double price, double neutral_price, double bpw, double bvl) {
    return (bpw * neutral_price * (bvl - bvl * std::cosh(((price/neutral_price-1) *  ln2)/bvl)))/ln2;
}

double Strategy_Pile::calcBoostPriceFromPos(double position, double neutral_price, double bpw, double bvl) {
    return neutral_price - (neutral_price * bvl * std::asin(position/bpw))/ln2;
}

double Strategy_Pile::calcBoostNeutralFromValue(double position, double value, double price, double bpw, double bvl) {
    auto rtfn = [&](double x) {
      double km = ((price/x-1)*ln2)/bvl;
      double k0 = (bpw * x * (bvl - bvl * std::cosh(km)))/ln2;
      double k1 = bpw*((bvl-bvl*std::cosh(km))/ln2+(price*sinh(km))/x);
      return x - k0/k1;
    };

    if (value >= 0) return price;
    double x0 = position<0?price*0.5:price*2; // guess
    for (int i = 0; i < 200; i++) {
        double x1 = rtfn(x0);
        if (std::abs(x1-x0)/price < 1e-8) return x1;
        x0 = x1;
    }
    return x0;
}

bool Strategy_Pile::Config::isBoostEnabled() const {
    return boost_power>0 && boost_volatility > 0;
}

double Strategy_Pile::calcBoostNeutralFromPos(double position, double price, double bpw, double bvl) {
    return (price * ln2)/(-bvl* std::asinh(position/bpw) + ln2);
}
