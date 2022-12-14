/*
 * strategy_exponencial.cpp
 *
 *  Created on: 27. 8. 2022
 *      Author: ondra
 */

#include "strategy_exponencial.h"
#include <cmath>

#include <imtjson/object.h>
#include "sgn.h"

#include "numerical.h"


std::string_view Strategy_Exponencial::id = "expwide";

double Strategy_Exponencial::MathModule::findApxRangeEnd(double z) {
    //Approximation of integration range for given z (returns normalized)
    /*
     * z=1, result= 6.8
     * z=2, result= 2.94
     * z=4, result= 1.731
     * etc..
     */
    return 5.6*1/std::pow(z, 1.7)+1.2;
}

double Strategy_Exponencial::MathModule::baseFn(double x) const {
    return std::exp(-std::pow(x, z))/std::pow(x,w);
}

double Strategy_Exponencial::MathModule::baseFnEx(double x) const {
    if (x > range) {
        return baseFn(range) * range/x;
    } else {
        return baseFn(x);
    }
}

void Strategy_Exponencial::MathModule::initTable(double min, double max) {
//    double xs = static_cast<double>(1.0)/static_cast<double>(intTableSize)*0.5;
    double fna = baseFn(min);
    if (!std::isfinite(fna)) fna = 0;
    double sum = 0;
    double ia = min;
    int_table[0] = 0;
    for (std::size_t i = 0; i < intTableSize; i++) {
        double ib = static_cast<double>(i+1)/static_cast<double>(intTableSize)*(max-min)+min;
        double fnb = baseFn(ib);
        double fnc = baseFn((2*ia+ib)/3.0);
        double fnd = baseFn((ia+2*ib)/3.0);
        double r = (ib - ia)*(fna+3*fnc+3*fnd+fnb)/8.0;
        ia = ib;
        fna = fnb;
        sum += r;
        int_table[i+1] = sum;
    }
}

double Strategy_Exponencial::MathModule::primFn(double x) const {
    if (x < 0) return int_table[0];
    if (x > range) return int_table[intTableSize];
    double findex = x/range*intTableSize;
    double iindex;
    double part = std::modf(findex, &iindex);
    auto i = static_cast<std::size_t>(iindex);
    double r1 = int_table[i];
    double r2 = int_table[i+1];
    return r1 + (r2 - r1) * part;
}

double Strategy_Exponencial::MathModule::primFnEx(double x) const {
    if (x > range) {
        return primFn(range) + baseFn(range)*range*std::log(x/range);
    } else {
        return primFn(x);
    }
}

double Strategy_Exponencial::MathModule::invFn(double y) const {
    return numeric_search_r1(range, [&](double x){
        return baseFn(x)-y;
    });

}
double Strategy_Exponencial::MathModule::invFnEx(double y) const {
    double sr = baseFn(range);
    if (y>sr) {
        return invFn(y);
    } else {
        return (range * sr)/y;
    }
}

double Strategy_Exponencial::MathModule::findEquity(double eq, double k, double w) const {
    double r = calcRange(k);
    double sr = calcEquity(r,k,w);
    if (eq<sr) {
        return numeric_search_r1(r, [&](double x){
           return calcEquity(x, k, w) - eq;
        });
    } else if (eq > sr) {
        return numeric_search_r2(r, [&](double x){
           return calcEquity(x, k, w) - eq;
        });

    } else {
        return r;
    }
}

double Strategy_Exponencial::MathModule::calcPos(double x, double k, double w) const {
    return baseFnEx(x/k)*w/k;
}

double Strategy_Exponencial::MathModule::calcEquity(double x, double k, double w) const {
    return primFnEx(x/k)*w;
}

double Strategy_Exponencial::MathModule::findPos(double y, double k, double w) const{
    return invFnEx(y/w*k)*k;
}

double Strategy_Exponencial::MathModule::calcCurr(double x, double k, double w) const {
    double eq = calcEquity(x, k, w);
    double v = x * calcPos(x, k, w);
    return eq - v;
}

double Strategy_Exponencial::MathModule::calcRange(double k) const {
    return k * range;
}

double Strategy_Exponencial::MathModule::findCurrency(double c, double k, double w) const {
    double r = calcRange(k);
    double sr = calcCurr(r,k,w);
    if (c<sr) {
        return numeric_search_r1(r, [&](double x){
           return calcCurr(x, k, w) - c;
        });
    } else if (c < sr) {
        return numeric_search_r2(r, [&](double x){
           return calcCurr(x, k, w) - c;
        });

    } else {
        return r;
    }
}

double Strategy_Exponencial::MathModule::calcRatio(double x, double k) const {
    return calcPos(x, k, 1.0)*x / calcEquity(x, k, 1.0);
}
double Strategy_Exponencial::MathModule::findRatio(double c, double k) const {
    double r = calcRange(k);
    double sr = calcRatio(r,k);
    if (c>sr) {
        return numeric_search_r1(r, [&](double x){
           return calcRatio(x, k) - c;
        });
    } else if (c < sr) {
        return numeric_search_r2(r, [&](double x){
           return calcRatio(x, k) - c;
        });

    } else {
        return r;
    }
}


Strategy_Exponencial::PMathModule Strategy_Exponencial::prepareMath(const Config &cfg) {
    MathModule m;
    m.Config::operator=(cfg);
    double srchEnd = m.findApxRangeEnd(cfg.z);
    m.initTable(0, srchEnd);
    double min_ratio = m.baseFn(srchEnd)*srchEnd/m.int_table[intTableSize];
    m.r = std::max(min_ratio, cfg.r);
    m.range = srchEnd;
    if (m.r>min_ratio) {
        double nr = numeric_search_r1(srchEnd, [&](double x) {
            double myr = m.baseFn(x) * x / m.primFn(x);
            return myr - cfg.r;
        });
        m.initTable(0, nr);
        m.range = nr;
    }
    return std::make_shared<MathModule>(m);
}

Strategy_Exponencial::Strategy_Exponencial(const Config &cfg)
:mcfg(prepareMath(cfg))
{
}

Strategy_Exponencial::Strategy_Exponencial(const PMathModule &math, State &&st)
:mcfg(math), st(std::move(st))
{
}

bool Strategy_Exponencial::isValid() const {
    return st.b > 0 && st.k > 0 && st.p > 0;
}

PStrategy Strategy_Exponencial::reset() const {
    return new Strategy_Exponencial(mcfg,{});
}

PStrategy Strategy_Exponencial::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
    return new Strategy_Exponencial(mcfg, State {
        !minfo.leverage,
        src["k"].getNumber(),
        src["p"].getNumber(),
        src["b"].getNumber(),
        src["m"].getNumber(),
    });
}

json::Value Strategy_Exponencial::exportState() const {
    return json::Object{
        {"p",st.p},
        {"k",st.k},
        {"b",st.b},
        {"m",st.m}
    };
}

std::string_view Strategy_Exponencial::getID() const {
    return id;
}

PStrategy Strategy_Exponencial::onIdle(const IStockApi::MarketInfo &minfo,
        const IStockApi::Ticker &curTicker, double assets,
        double currency) const {
    if (!isValid()) {
        return init(!minfo.leverage, curTicker.last, assets,
                    minfo.leverage?currency:assets*curTicker.last+currency)
                ->onIdle(minfo, curTicker, assets, currency);
    } else {
        return this;
    }
}

IStrategy::OrderData Strategy_Exponencial::getNewOrder(
        const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
        double dir, double assets, double currency, bool rej) const {

    double newpos = mcfg->calcPos(new_price, st.k, st.m);
    return {
        0, newpos - assets
    };

}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_Exponencial::onTrade(
        const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
        double assetsLeft, double currencyLeft) const {

    double prevPos = assetsLeft - tradeSize;
    double eqchg = prevPos * (tradePrice - st.p);
    double budget = mcfg->calcEquity(tradePrice, st.k, st.m);
    double rprice = mcfg->calcRange(st.k);
    State nst;
    nst.spot = !minfo.leverage;
    nst.p = tradePrice;
    nst.b = budget;
    if (rprice < tradePrice) {
        nst.k = tradePrice/mcfg->range;
        nst.m =  nst.b/mcfg->calcEquity(tradePrice, nst.k,  1.0);
    } else if (mcfg->s < 0 && tradePrice < st.p && tradePrice < st.k) {
        double rt = tradePrice/st.p;
        double refp = rt * st.k;
        double eq1 = mcfg->calcEquity(st.k, st.k, st.m);
        double eq2 = mcfg->calcEquity(refp, st.k, st.m);
        budget=std::min(eq1, budget+(eq1-eq2)*(-mcfg->s));
        double newk = numeric_search_r1(st.k*1.1, [&](double nk){
            return mcfg->calcEquity(tradePrice, nk, st.m) - budget;
        });
        nst.k = newk;
        nst.m = st.m;
        nst.b = budget;
    } else if (rprice * mcfg->s > tradePrice) {
        nst.k = tradePrice/(mcfg->s*mcfg->range);
        nst.m =  nst.b/mcfg->calcEquity(tradePrice, nst.k,  1.0);
    } else {
        nst.k = st.k;
        nst.m = st.m;
    }
    double np = st.b + eqchg - nst.b;
    return {
        {np,0},
        new Strategy_Exponencial(mcfg, std::move(nst))
    };


}

IStrategy::MinMax Strategy_Exponencial::calcSafeRange(
        const IStockApi::MarketInfo &minfo, double assets,
        double currencies) const {

    MinMax minMax;
    double a = mcfg->calcPos(st.p, st.k, st.m);
    double minsz = minfo.calcMinSize(st.p);
    if (assets < a + minsz ) {
        minMax.max = mcfg->findPos(a - assets + minsz, st.k, st.m);
        if (minMax.max/1000 > st.p) minMax.max = std::numeric_limits<double>::infinity();
    } else {
        minMax.max = std::numeric_limits<double>::infinity();
    }

    if (st.spot) {
        double c = mcfg->calcCurr(st.p, st.k, st.m);
        if (c > currencies) {
            minMax.min = mcfg->findCurrency(c - currencies,  st.k, st.m);
        } else {
            minMax.min = 0;
        }
    } else {
        double c = mcfg->calcEquity(st.p, st.k, st.m);
        if (c > currencies) {
            minMax.min = mcfg->findEquity(c - currencies, st.k, st.m);
        } else {
            minMax.min = 0;
        }
    }
    if (minMax.max*1000 < st.p) minMax.min = 0;
    return minMax;
}

double Strategy_Exponencial::calcCurrencyAllocation(double price, bool leveraged) const {
    if (st.spot) {
        return mcfg->calcCurr(st.p, st.k, st.m);
    } else {
        return mcfg->calcEquity(st.p, st.k, st.m);
    }
}

double Strategy_Exponencial::getCenterPrice(double lastPrice, double assets) const {
    return mcfg->findPos(assets, st.k, st.m);
}

double Strategy_Exponencial::calcInitialPosition(
        const IStockApi::MarketInfo &minfo, double price, double assets,
        double currency) const {

    double p = mcfg->calcPos(price, price, 1.0);
    double b = mcfg->calcEquity(price, price, 1.0);
    double eq = minfo.leverage?currency:currency+price*assets;
    return p*eq/b;

}

IStrategy::BudgetInfo Strategy_Exponencial::getBudgetInfo() const {
    return {
        st.b,
        mcfg->calcPos(st.p, st.k, st.m)
    };
}

double Strategy_Exponencial::getEquilibrium(double assets) const {
    return mcfg->findPos(assets, st.k, st.m);
}

IStrategy::ChartPoint Strategy_Exponencial::calcChart(double price) const {
    return {
        true,
        mcfg->calcPos(price, st.k, st.m),
        mcfg->calcEquity(price, st.k, st.m)
    };
}

json::Value Strategy_Exponencial::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
    auto price = [&](double x){return minfo.invert_price?1.0/x:x;};
    auto position = [&](double x){return minfo.invert_price?-x:x;};
    double pos = mcfg->calcPos(st.p,st.k,st.m);
    return json::Object{
        {"Budget",st.b},
        {"Position",position(pos)},
        {"Expand price", price(mcfg->calcRange(st.k))},
        {"Shrink price", price(mcfg->calcRange(st.k)*mcfg->s)},
        {"Ratio", 100*pos*st.p / st.b}
    };
}

PStrategy Strategy_Exponencial::init(bool spot, double price, double pos, double equity) const {

    double ratio = pos*price/equity;
    double p = mcfg->findRatio(ratio, price);

    State st;
    st.spot = spot;
    st.k = pow2(price)/p;
    st.p = price;
    st.b = equity;
    st.m = pos/mcfg->calcPos(price, st.k, 1);

    PStrategy s = new Strategy_Exponencial(mcfg, std::move(st));
    if (!s->isValid()) throw std::runtime_error("Unable to initialize strategy");
    return s;

}
