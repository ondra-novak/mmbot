/*
 * strategy_constantstep.cpp
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#include "strategy_constantstep.h"

#include <cmath>
#include "../imtjson/src/imtjson/object.h"
#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"

#include "sgn.h"
using json::Value;

std::string_view Strategy_ConstantStep::id = "conststep";


Strategy_ConstantStep::Strategy_ConstantStep(const Config &cfg, State &&st)
	:cfg(cfg),st(std::move(st))
{
}

Strategy_ConstantStep::Strategy_ConstantStep(const Config &cfg)
	:cfg(cfg)
{
}

bool Strategy_ConstantStep::isValid() const {
	return st.k > 0 && st.w > 0 && st.p>0;
}

PStrategy Strategy_ConstantStep::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		return init(!minfo.leverage,ticker.last, assets,currency);
	}
}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_ConstantStep::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

    State nst = st;    
    if (tradePrice > nst.k) {
        nst.k = tradePrice;        
    }
    nst.p = tradePrice;
    double prevPos = assetsLeft - tradeSize;
    double pnl = prevPos * (nst.p - st.p);
    double pb = calcBudget(st.k, st.w, st.p);
    double nb = calcBudget(nst.k, nst.w, nst.p);
    double na = pnl - nb + pb;
    return {
        {na,0},
        new Strategy_ConstantStep(cfg, std::move(nst))
    };
}

json::Value Strategy_ConstantStep::exportState() const {
	return json::Object({
		{"p",st.p},
		{"w",st.w},
		{"k",st.k}
	});
}



PStrategy Strategy_ConstantStep::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	State newst {
		src["k"].getNumber(),
		src["w"].getNumber(),
		src["p"].getNumber()
	};
	return new Strategy_ConstantStep(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_ConstantStep::getNewOrder(const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets,
		double currency, bool rej) const {
    double pos = calcPos(st.k, st.w, new_price);
    double diff = pos - assets;
    return {0, diff, new_price >= st.k?Alert::forced:Alert::enabled};
}

IStrategy::MinMax Strategy_ConstantStep::calcSafeRange(const IStockApi::MarketInfo &minfo,
		double assets, double currencies) const {
    double pos = calcPos(st.k, st.w, st.p);
    double max;
    double minsz = minfo.calcMinSize(st.p);
    if (pos > assets+minsz) {
        max = calcPosInv(st.k, st.w, pos - assets);
    } else {
        max = st.k;
    }
    double min;
    double cur = calcCur(st.k, st.w, st.p);
    if (cur > currencies+minsz*st.p) {
        min = calcCurInv(st.k, st.w, cur - currencies);
    } else {
        min = 0;
    }
	return {min,max};
}

double Strategy_ConstantStep::getEquilibrium(double assets) const {
    return calcPosInv(st.k, st.w, assets);
}

PStrategy Strategy_ConstantStep::reset() const {
	return new Strategy_ConstantStep(cfg,{});
}

std::string_view Strategy_ConstantStep::getID() const {
	return id;
}

json::Value Strategy_ConstantStep::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {
    
    auto pos = [&](double x) {return (minfo.invert_price?-1:1)* x;};
    auto price = [&](double x) {return (minfo.invert_price?1/x:x);};
    
    return json::Object{
        {"Current equity", calcBudget(st.k,st.w, st.p)},
        {"Max equity", st.w},
        {"Sell all price", price(st.k)},
        {"Current position", pos(calcPos(st.k, st.w, st.p))},
    };    
}

PStrategy Strategy_ConstantStep::init(bool spot,double price, double assets, double cur) const {
    double equity = (spot?price*assets:0) + cur;
    double ratio = assets * price / equity;
    double k = (price * (ratio - 2))/(2 * (ratio - 1));
    double b = calcBudget(k, 1, price);
    double w = equity/b;
    if (ratio >=1.0) throw std::runtime_error("Can't initialize strategy with zero currency or with leverage above 1x");
    
    State nst;
    nst.k = k;
    nst.p = price;
    nst.w = w;
    PStrategy s = new Strategy_ConstantStep(cfg, std::move(nst));
    if (s->isValid()) {
        return s;
    } else {
        throw std::runtime_error("Failed to initialize strategy");
    }
}

double Strategy_ConstantStep::calcInitialPosition(const IStockApi::MarketInfo& ,  double , double , double ) const {
    return 0;
}

IStrategy::BudgetInfo Strategy_ConstantStep::getBudgetInfo() const {
	return BudgetInfo {
		st.w,
		0
	};
}


double Strategy_ConstantStep::calcCurrencyAllocation(double price, bool leveraged) const {
	if (leveraged) calcBudget(st.k, st.p, price);
    return calcCur(st.k, st.w, st.p);
}

Strategy_ConstantStep::ChartPoint Strategy_ConstantStep::calcChart(double price) const {
	return {
		true,
		calcPos(st.k, st.w, price),
		calcBudget(st.k, st.w, price)
	};
}

double Strategy_ConstantStep::calcPos(double k, double w, double price) {
    if (price>=k) return 0;
    return 2*w/k*(1-price/k);
}

double Strategy_ConstantStep::calcBudget(double k, double w, double price) {
    if (price>=k) return w;
    return 2*w/k*(price - pow2(price)/(2*k));
}

double Strategy_ConstantStep::calcPosInv(double k, double w, double pos) {
    if (pos < 0) return k;
    return k - (pos * pow2(k))/(2 * w);
}

double Strategy_ConstantStep::calcBudgetInv(double k, double w, double budget) {
    if (budget > w) return k;
    return (k * w - std::sqrt(pow2(k) * w * (w - budget)))/w;
}

double Strategy_ConstantStep::calcCur(double k, double w, double price) {
    return (w * pow2(price))/pow2(k);
}

double Strategy_ConstantStep::calcCurInv(double k, double w, double cur) {
    if (cur <= 0) return 0;
    return k*std::sqrt(cur)/std::sqrt(w);
}
