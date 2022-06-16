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

Strategy_Pile::Strategy_Pile(const Config &cfg):cfg(cfg),bpw(0) {}
Strategy_Pile::Strategy_Pile(const Config &cfg, State &&st)
    :cfg(cfg),st(std::move(st)) {
    bpw = calcBPW(this->st);
}

std::string_view Strategy_Pile::id = "pile";

PStrategy Strategy_Pile::init(double price, double assets, double currency, bool leveraged) const {

    State nst = {};
    nst.budget = (leveraged?0:(price*assets))+currency;
    nst.kmult = calcKMult(price, nst.budget, cfg.ratio);
    nst.lastp = price;
    if (cfg.isBoostEnabled()) {
        nst.boost_neutral_price = price;
        double pos = assets - calcPosition(cfg.ratio, nst.kmult, price);
        double bpw = calcBPW(nst);
        nst.boost_neutral_price = calcBoostNeutralFromPos(pos, price, bpw, cfg.boost_volatility);
        nst.boost_value = calcBoostValue(price, nst.boost_neutral_price, bpw, cfg.boost_volatility);
        nst.boost_pos = pos;
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
	return kmult*ratio*std::pow(price, ratio-1);
}

double Strategy_Pile::calcBudget(double ratio, double kmult, double price) {
	return kmult*std::pow(price,ratio);
}

double Strategy_Pile::calcEquilibrium(double ratio, double kmul, double position) {
    //(c/(k z))^(1/(-1 + z))
    return std::pow<double>(position/(kmul*ratio),1.0/(ratio-1));
}

double Strategy_Pile::calcPriceFromBudget(double ratio, double kmul, double budget) {
    //(c/k)^(1/z)
	return std::pow(budget/kmul, 1.0/(ratio));
}

double Strategy_Pile::calcCurrency(double ratio, double kmult, double price) {
	return kmult*std::pow(price,ratio)*(1 - ratio);
}

double Strategy_Pile::calcPriceFromCurrency(double ratio, double kmult, double currency) {
    //((k - k z)/c)^(-1/z)
	return std::pow((kmult - kmult*ratio)/currency,-1.0/ratio);
}



double Strategy_Pile::calcNewK(double pos, double price, double pl, bool no_profit) const {
    if (pl == 0
        || pos == 0
        || (price-st.boost_neutral_price) * (st.lastp - st.boost_neutral_price) < 0)
            return st.boost_neutral_price;



    double profit = no_profit?0:calcBoostValue(st.boost_neutral_price*std::exp(st.boost_spread), st.boost_neutral_price, bpw, cfg.boost_volatility);
    pl += profit;
    if (pl > 0) return st.boost_neutral_price;

    double nval = st.boost_value+pl;


    if (nval >= 0) {
        return price;
    }

    double res =calcBoostNeutralFromValue(pos, nval, price, bpw, cfg.boost_volatility);
    if (!std::isfinite(res)) return st.boost_neutral_price;

    if (std::abs(res-price)>std::abs(st.boost_neutral_price-price))
        return st.boost_neutral_price;

    return res;


}

IStrategy::OrderData Strategy_Pile::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double finPos = calcPosition(cfg.ratio, st.kmult, new_price);
	double finPl = (new_price - st.lastp) * assets;
    double finBudget = calcBudget(cfg.ratio, st.kmult, new_price);

	if (cfg.isBoostEnabled()) {
	    double pos = st.boost_pos;
	    double bpl = (new_price - st.lastp) * pos;
        double newk = calcNewK(pos, new_price, bpl, false);
        double npos = calcBoostPosition(new_price, newk, bpw, cfg.boost_volatility);
	    finPos += npos;
	    finBudget += st.boost_value + bpl;
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
	nst.lastp = tradePrice;


	if (cfg.isBoostEnabled()) {
        double pos = st.boost_pos;
/*        if (!minfo.leverage && tradeSize == 0) {
            if (assetsLeft <= 0) {
                pos = -calcPosition(cfg.ratio, st.kmult, tradePrice);
            }
        }*/

	    double hhpp = assetsLeft-tradeSize-pos;
	    double hhplch = (tradePrice - st.lastp) * hhpp;
	    nst.budget = calcBudget(cfg.ratio, st.kmult, tradePrice);
        double budgetchange = nst.budget - st.budget;;
        np = hhplch - budgetchange;

        double bbplch = (tradePrice - st.lastp) * pos;
        double newk = calcNewK(pos, tradePrice, bbplch, std::abs(pos*tradePrice)>nst.budget);
        nst.boost_neutral_price = newk;
        double bpw = calcBPW(nst);
        nst.boost_value = calcBoostValue(tradePrice, newk, bpw, cfg.boost_volatility);
        nst.boost_pos = calcBoostPosition(tradePrice, newk, bpw, cfg.boost_volatility);
        np += bbplch - (nst.boost_value - st.boost_value);
        double curspread =std::abs(std::log(tradePrice/st.lastp));
        nst.boost_spread=st.boost_spread<=0?curspread:(nst.boost_spread*199.0+curspread)/200.0;
	} else {
	    double prevpos = assetsLeft - tradeSize;
	    double plchange = (tradePrice - st.lastp) * prevpos;
	    nst.budget = calcBudget(cfg.ratio, st.kmult, tradePrice);
	    double budgetchange = nst.budget - st.budget;;
	    np = plchange - budgetchange;
	    nst.boost_neutral_price = 0;
	    nst.boost_value = 0;
	    nst.boost_pos = 0;
	}

	np += st.np_hidden;
	if (tradeSize || np<0) {
	    nst.np_hidden = 0;
	} else {
	    nst.np_hidden = np;
	    np = 0;
	}

	nst.lastp = tradePrice;
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
    nst.np_hidden= src["np_hidden"].getNumber();
    nst.kmult = calcKMult(nst.lastp, nst.budget, cfg.ratio);
    if (cfg.isBoostEnabled()) {
        auto boost = src["boost"];
        nst.boost_pos = boost["p"].getNumber();
        nst.boost_neutral_price = boost["k"].getNumber();
        nst.boost_value = boost["v"].getNumber();
        nst.boost_spread = boost["s"].getNumber();
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
	return st.budget > 0 && st.kmult > 0 && st.lastp > 0 && (!cfg.isBoostEnabled() || st.boost_neutral_price > 0);
}

json::Value Strategy_Pile::exportState() const {
	return json::Object {
		{"lastp",st.lastp},
		{"budget",st.budget},
		{"np_hidden", st.np_hidden},
		{"boost", json::Object {
		    {"k", st.boost_neutral_price},
		    {"p", st.boost_pos},
		    {"v", st.boost_value},
            {"s", st.boost_spread}
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
    if (cfg.isBoostEnabled()) {
        assets -= calcPosition(cfg.ratio, st.kmult, st.lastp);
        return calcBoostPriceFromPos(assets, st.boost_neutral_price, bpw, cfg.boost_volatility);
    } else {
        if (assets < 0) return st.lastp*2;
	    return calcEquilibrium(cfg.ratio, st.kmult, assets);
    }

}

double Strategy_Pile::calcCurrencyAllocation(double price, bool leveraged) const {
    double eq1 = calcBudget(cfg.ratio, st.kmult, price);
    double eq2 = cfg.isBoostEnabled()?calcBoostValue(price, st.boost_neutral_price, cfg.boost_power * st.budget/st.boost_neutral_price, cfg.boost_volatility):0;
    double eq = eq1 + eq2;
    if (leveraged) return eq;
    double pos = calcPosition(cfg.ratio, st.kmult, price) + st.boost_pos;
	return std::max(0.0,eq - pos * price);
}

IStrategy::ChartPoint Strategy_Pile::calcChart(double price) const {
	return ChartPoint{
		true,
		calcPosition(cfg.ratio, st.kmult, price) + (cfg.isBoostEnabled()?calcBoostPosition(price, st.boost_neutral_price, cfg.boost_power*st.budget/st.boost_neutral_price, cfg.boost_volatility):0),
		calcBudget(cfg.ratio, st.kmult, price) + (cfg.isBoostEnabled()?calcBoostValue(price, st.boost_neutral_price,  cfg.boost_power*st.budget/st.boost_neutral_price, cfg.boost_volatility):0)
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
    auto pprice = [&](double x) {return minfo.invert_price?1.0/x:x;};
    auto ppos = [&](double x) {return  minfo.invert_price?-x:x;};
    double tp = st.boost_pos + calcPosition(cfg.ratio, st.kmult, st.lastp);
	return json::Object{
		{"Budget",st.budget},
		{"Multiplier",st.kmult},
		{"boost.Value", st.boost_value},
		{"boost.Neutral", pprice(st.boost_neutral_price)},
        {"Position", ppos(tp)},
		{"boost.Position", ppos(st.boost_pos)},
        {"Current ratio%", (tp*st.lastp / st.budget)*100}
	};
}

double Strategy_Pile::calcBoostPosition(double price, double neutral_price, double bpw, double bvl) {
    return bpw*std::sinh(ln2*(1-price/neutral_price)/bvl);
}

double Strategy_Pile::calcBoostValue(double price, double neutral_price, double bpw, double bvl) {
    return (bpw * neutral_price * (bvl - bvl * std::cosh(((price/neutral_price-1) *  ln2)/bvl)))/ln2;
}

double Strategy_Pile::calcBoostPriceFromPos(double position, double neutral_price, double bpw, double bvl) {
    double s =  neutral_price - (neutral_price * bvl * std::asinh(position/bpw))/ln2;
    return s;
}

double Strategy_Pile::calcBoostNeutralFromValue(double position, double value, double price, double bpw, double bvl) {
    auto rtfn = [&](double x) {
      double km = ((price/x-1)*ln2)/bvl;
      double k0 = (bpw * x * (bvl - bvl * std::cosh(km)))/ln2 - value;
      double k1 = bpw*((bvl-bvl*std::cosh(km))/ln2+(price*sinh(km))/x);
      return x - k0/k1;
    };

    if (value >= 0) return price;
    double x0 = position<0?price*0.5:price*2; // guess
    for (int i = 0; i < 200; i++) {
        double x1 = rtfn(x0);
        bool e = std::abs(x1-x0)/price < 1e-8;
        x0 = x1;
        if (e) break;
    }
    return x0;
}

bool Strategy_Pile::Config::isBoostEnabled() const {
    return boost_power>0 && boost_volatility > 0;
}

double Strategy_Pile::calcBoostNeutralFromPos(double position, double price, double bpw, double bvl) {
    double s = (price * ln2)/(-bvl* std::asinh(position/bpw) + ln2);
    return s;
}

double Strategy_Pile::calcBPW(const State &st) const {
    double budget = calcBudget(cfg.ratio, st.kmult, st.lastp);
    double bpw = budget/st.boost_neutral_price * cfg.boost_power;
    return bpw;
}
