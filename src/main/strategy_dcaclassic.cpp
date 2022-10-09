/*
 * strategy_ConstantStep<fn>.cpp
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#include <cmath>
#include "../imtjson/src/imtjson/object.h"
#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"

#include "sgn.h"
#include "strategy_dcaclassic.h"
using json::Value;



template<>
std::string_view Strategy_DCA<DCAFunction::lin_amount>::id = "conststep";
template<>
std::string_view Strategy_DCA<DCAFunction::lin_value>::id = "dcavalue";
template<>
std::string_view Strategy_DCA<DCAFunction::lin_volume>::id = "dcavolume";
template<>
std::string_view Strategy_DCA<DCAFunction::martingale>::id = "dcamartingale";


template<DCAFunction fn>
Strategy_DCA<fn>::Strategy_DCA(const Config &cfg, State &&st)
	:cfg(cfg),st(std::move(st))
{
}

template<DCAFunction fn>
Strategy_DCA<fn>::Strategy_DCA(const Config &cfg)
	:cfg(cfg)
{
}

template<DCAFunction fn>
bool Strategy_DCA<fn>::isValid() const {
	return st.k > 0 && st.w > 0 && st.p>0;
}

template<DCAFunction fn>
PStrategy Strategy_DCA<fn>::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		return init(!minfo.leverage,ticker.last, assets,currency);
	}
}

template<DCAFunction fn>
std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_DCA<fn>::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

    State nst = st;    
    double prevPos = assetsLeft - tradeSize;    
    adjust_state(nst, tradePrice, tradeSize, prevPos);
    
    nst.p = tradePrice;
    double pnl = prevPos * (nst.p - st.p);
    double pb = calcBudget(cfg, st.k, st.w, st.p);
    double nb = calcBudget(cfg, nst.k, nst.w, nst.p);
    double na = pnl - nb + pb;
    return {
        {na,0, nst.k},
        new Strategy_DCA<fn>(cfg, std::move(nst))
    };
}

template<DCAFunction fn>
void Strategy_DCA<fn>::adjust_state(State &nst, double tradePrice, double tradeSize, double prevPos) const {
    if (tradePrice > nst.k) {
        nst.k = tradePrice;        
    }
}



template<DCAFunction fn>
json::Value Strategy_DCA<fn>::exportState() const {
	return json::Object({
		{"p",st.p},
		{"w",st.w},
		{"k",st.k},
        {"hlp",st.hlp?json::Value(st.hlp):json::Value()}
	});
}



template<DCAFunction fn>
PStrategy Strategy_DCA<fn>::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	State newst {
		src["k"].getNumber(),
		src["w"].getNumber(),
		src["p"].getNumber(),
		src["hlp"].getNumber()
	};
	return new Strategy_DCA<fn>(cfg, std::move(newst));
}

template<DCAFunction fn>
IStrategy::OrderData Strategy_DCA<fn>::getNewOrder(const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets,
		double currency, bool rej) const {
    double pos = calcPos(cfg, st.k, st.w, new_price);
    double diff = pos - assets;
    return {0, diff, new_price >= st.k?Alert::forced:Alert::enabled};
}

template<DCAFunction fn>
IStrategy::MinMax Strategy_DCA<fn>::calcSafeRange(const IStockApi::MarketInfo &minfo,
		double assets, double currencies) const {
    double pos = calcPos(cfg, st.k, st.w, st.p);
    double max;
    double minsz = minfo.calcMinSize(st.p);
    if (pos > assets+minsz) {
        max = calcPosInv(cfg, st.k, st.w, pos - assets);
    } else {
        max = st.k;
    }
    double min;
    double cur = calcCur(cfg, st.k, st.w, st.p);
    if (cur > currencies+minsz*st.p) {
        min = calcCurInv(cfg, st.k, st.w, cur - currencies);
    } else {
        min = 0;
    }
	return {min,max};
}

template<DCAFunction fn>
double Strategy_DCA<fn>::getCenterPrice(double lastPrice, double assets) const  {
    if (lastPrice > st.k) {
        return lastPrice;
    }
    else {
        return getEquilibrium(assets);
    }
}

template<DCAFunction fn>
double Strategy_DCA<fn>::getEquilibrium(double assets) const {
    return calcPosInv(cfg, st.k, st.w, assets);
}

template<DCAFunction fn>
PStrategy Strategy_DCA<fn>::reset() const {
	return new Strategy_DCA<fn>(cfg,{});
}

template<DCAFunction fn>
std::string_view Strategy_DCA<fn>::getID() const {
	return id;
}

template<DCAFunction fn>
json::Value Strategy_DCA<fn>::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {
    
    auto pos = [&](double x) {return (minfo.invert_price?-1:1)* x;};
//    auto price = [&](double x) {return (minfo.invert_price?1/x:x);};
    
    return json::Object{
        {"Current equity", calcBudget(cfg, st.k,st.w, st.p)},
        {"Max equity", st.w},
        {"Current position", pos(calcPos(cfg, st.k, st.w, st.p))},
    };    
}

template<DCAFunction fn>
PStrategy Strategy_DCA<fn>::init(bool spot,double price, double assets, double cur) const {
    double equity = (spot?price*assets:0) + cur;
    double ratio = assets * price / equity;
    if (ratio >=1.0) throw std::runtime_error("Can't initialize strategy with zero currency or with leverage above 1x");
    double k = findKFromRatio(cfg, price, ratio);
    double b = calcBudget(cfg, k, 1, price);
    double w = equity/b;
    
    State nst;
    nst.k = k;
    nst.p = price;
    nst.w = w;
    PStrategy s = new Strategy_DCA<fn>(cfg, std::move(nst));
    if (s->isValid()) {
        return s;
    } else {
        throw std::runtime_error("Failed to initialize strategy");
    }
}

template<DCAFunction fn>
double Strategy_DCA<fn>::calcInitialPosition(const IStockApi::MarketInfo& ,  double , double , double ) const {
    return 0;
}

template<DCAFunction fn>
IStrategy::BudgetInfo Strategy_DCA<fn>::getBudgetInfo() const {
	return BudgetInfo {
		st.w,
		0
	};
}


template<DCAFunction fn>
double Strategy_DCA<fn>::calcCurrencyAllocation(double price, bool leveraged) const {
	if (leveraged) calcBudget(cfg, st.k, st.p, price);
    return calcCur(cfg, st.k, st.w, st.p);
}

template<DCAFunction fn>
typename Strategy_DCA<fn>::ChartPoint Strategy_DCA<fn>::calcChart(double price) const {
	return {
		true,
		calcPos(cfg, st.k, st.w, price),
		calcBudget(cfg, st.k, st.w, price)
	};
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::calcPos(const Config &cfg, double k, double w, double price) {
    if (price>=k) return 0;
    return 2*w/k*(1-price/k);
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::calcBudget(const Config &cfg, double k, double w, double price) {
    if (price>=k) return w;
    return 2*w/k*(price - pow2(price)/(2*k));
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::calcPosInv(const Config &cfg, double k, double w, double pos) {
    if (pos < 0) return k;
    return k - (pos * pow2(k))/(2 * w);
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::calcCur(const Config &cfg, double k, double w, double price) {
    return (w * pow2(price))/pow2(k);
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::calcCurInv(const Config &cfg, double k, double w, double cur) {
    if (cur <= 0) return 0;
    return k*std::sqrt(cur)/std::sqrt(w);
}

template<>
double Strategy_DCA<DCAFunction::lin_amount>::findKFromRatio(const Config &, double price, double ratio) {
    return (price * (ratio - 2))/(2 * (ratio - 1));
}


//--------------

template<>
double Strategy_DCA<DCAFunction::lin_value>::calcPos(const Config &cfg, double k, double w, double price) {
    if (price>=k) return 0;
    return -w*(k-price)/(k*price*std::log(cfg.max_drop));
}

template<>
double Strategy_DCA<DCAFunction::lin_value>::calcBudget(const Config &cfg, double k, double w, double price) {
    if (price>=k) return w;
    return -w/(std::log(cfg.max_drop)) * (1 - price/k + std::log(price/k)) + w;
}

template<>
double Strategy_DCA<DCAFunction::lin_value>::calcPosInv(const Config &cfg, double k, double w, double pos) {
    if (pos < 0) return k;
    return   (k * w)/(w - pos * k * std::log(cfg.max_drop));
}

template<>
double Strategy_DCA<DCAFunction::lin_value>::calcCur(const Config &cfg, double k, double w, double price) {
    return (w * std::log((cfg.max_drop * k)/price))/std::log(cfg.max_drop);
}

template<>
double Strategy_DCA<DCAFunction::lin_value>::calcCurInv(const Config &cfg, double k, double w, double cur) {
    return k * std::pow(cfg.max_drop,1 - cur/w);
}

template<>
void Strategy_DCA<DCAFunction::lin_value>::adjust_state(State &nst, double tradePrice, double tradeSize, double prevPos) const {
    if (tradePrice > nst.k) {
        nst.k = tradePrice;        
    }
    if (tradeSize == 0) {
        double min_price = nst.k * cfg.max_drop;
        if (tradePrice/min_price > 1.15 || tradePrice<st.p) return;
        nst.k = std::min(st.k, (19*st.k + tradePrice/cfg.max_drop)/20);        
    }
}


template<>
double Strategy_DCA<DCAFunction::lin_value>::findKFromRatio(const Config &cfg, double price, double ratio) {    
    double newk = numeric_search_r2(price, [&](double k){
        return calcPos(cfg, k, 1, price)*price/calcBudget(cfg, k, 1, price) - ratio;
    });
    return std::max(newk, price);
}
template<>
IStrategy::MinMax Strategy_DCA<DCAFunction::lin_value>::calcSafeRange(const IStockApi::MarketInfo &minfo,
        double assets, double currencies) const {
    double pos = calcPos(cfg, st.k, st.w, st.p);
    double max;
    double minsz = minfo.calcMinSize(st.p);
    if (pos > assets+minsz) {
        max = calcPosInv(cfg, st.k, st.w, pos - assets);
    } else {
        max = st.k;
    }
    double cur = calcCur(cfg, st.k, st.w, st.p);
    double min = calcCurInv(cfg, st.k, st.w, cur - currencies);
    return {min,max};
}

//---------------

template<>
double Strategy_DCA<DCAFunction::lin_volume>::calcPos(const Config &cfg, double k, double w, double price) {
    if (price>=k) return 0;
    return  w/k * std::log(k/price);
}

template<>
double Strategy_DCA<DCAFunction::lin_volume>::calcBudget(const Config &cfg, double k, double w, double price) {
    if (price>=k) return w;
    return (w * price * (1 + std::log(k/price)))/k; 
}

template<>
double Strategy_DCA<DCAFunction::lin_volume>::calcPosInv(const Config &cfg, double k, double w, double pos) {
    if (pos < 0) return k;
    return std::exp(-(k * pos)/w) * k;
}

template<>
double Strategy_DCA<DCAFunction::lin_volume>::calcCur(const Config &cfg, double k, double w, double price) {
    return w/k * price;
}

template<>
double Strategy_DCA<DCAFunction::lin_volume>::calcCurInv(const Config &cfg, double k, double w, double cur) {
    return (cur * k)/w;
}

template<>
double Strategy_DCA<DCAFunction::lin_volume>::findKFromRatio(const Config &, double price, double ratio) {
    return (price * (ratio - 2))/(2 * (ratio - 1));
}

//---------------


//z*e^(-z*x)*s/(x*z*e^-z)
static double martingaleBasicFn(const Strategy_DCA<DCAFunction::martingale>::Config &cfg, double price) {
    double pos = (cfg.exponent*std::exp(-cfg.exponent*price)*cfg.initial_step)/(price*cfg.exponent*std::exp(-cfg.exponent))*std::exp(-std::pow(price,cfg.cutoff));
    return pos;
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcPos(const Config &cfg, double k, double w, double price) {    
    return w/k*martingaleBasicFn(cfg, price/k);
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcBudget(const Config &cfg, double k, double w, double price) {
    double r = numeric_integral([&](double x){
        return martingaleBasicFn(cfg, x);
    }, 1, price/k, 100);
    return r*w+w;
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcPosInv(const Config &cfg, double k, double w, double pos) {
    if (pos <= 0) return k;   
    double npos = pos*k/w;
    double x =numeric_search_r1(10, [&](double x){
        return martingaleBasicFn(cfg, x)-npos;
    });
    return x*k;
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcCur(const Config &cfg, double k, double w, double price) {
    return calcBudget(cfg, k, w, price) - calcPos(cfg,k,w,price)*price;
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcCurInv(const Config &cfg, double k, double w, double cur) {
    double z =  numeric_search_r1(k*10, [&](double x) {
        return calcCur(cfg, k, w, x) - cur;
    });
    if (z<1e-100) {
        return k;
    } else {
        return z;
    }
}

template<>
double Strategy_DCA<DCAFunction::martingale>::findKFromRatio(const Config &cfg, double price, double ratio) {
    double minrat = calcPos(cfg,price,1,price)*price/calcBudget(cfg,price,1,price);
    if (minrat > ratio) return price;
    double start = price;
    double r1 = minrat;
    while (r1 < ratio) {
        start *= 1.1;
        r1 = calcPos(cfg,start,1,price)*price/calcBudget(cfg, start, 1, price);        
    }
    double c = numeric_search_r1(start, [&](double k) {
        return  calcPos(cfg,k,1,price)*price/calcBudget(cfg, k, 1, price)- ratio;
    });
    return c;
}

template<>
double Strategy_DCA<DCAFunction::martingale>::calcInitialPosition(const IStockApi::MarketInfo& minfo,  double price, double assets, double currency) const {
    double w = currency + (minfo.leverage?0:price * assets);
    return calcPos(cfg, price, w, price);
}


template<>
void Strategy_DCA<DCAFunction::martingale>::adjust_state(State &nst, double tradePrice, double tradeSize, double prevPos) const {
    if (tradePrice>nst.k) {
        if (tradePrice < nst.p) nst.k = tradePrice;
        return;    
    } else {
        if (tradeSize == 0 && tradePrice < nst.p) {
            if (nst.hlp == 0) nst.hlp = tradePrice; 
            return;
        } 
        double prevPrice = nst.hlp?nst.hlp:nst.p;
        nst.hlp = 0;
        double pnl = (tradePrice-prevPrice)*prevPos;        
        double oldb = calcBudget(cfg, nst.k, nst.w, prevPrice);
        double needb = oldb + pnl;
        if (tradePrice > prevPrice && tradeSize) needb -= cfg.initial_step*nst.w;
        if (needb >= nst.w) {
            nst.k = tradeSize?tradePrice:nst.k;
        } else {                  
            double k = numeric_search_r2(tradePrice, [&](double k){
                return calcBudget(cfg, k, nst.w, tradePrice) - needb;
            });
            nst.k = std::min(k, st.k);
        }
    }
    
}

template<>
IStrategy::MinMax Strategy_DCA<DCAFunction::martingale>::calcSafeRange(const IStockApi::MarketInfo &minfo,
        double assets, double currencies) const {
    double pos = calcPos(cfg, st.k, st.w, st.p);
    double max;
    double minsz = minfo.calcMinSize(st.p);
    if (pos < assets+minsz) pos = assets+minsz;  
    max = calcPosInv(cfg, st.k, st.w, pos - assets);
    double cur = calcCur(cfg, st.k, st.w, st.p);
    double min = calcCurInv(cfg, st.k, st.w, cur - currencies);
    return {min,max};
}


template class Strategy_DCA<DCAFunction::lin_amount>;
template class Strategy_DCA<DCAFunction::lin_value>;
template class Strategy_DCA<DCAFunction::lin_volume>;
template class Strategy_DCA<DCAFunction::martingale>;
