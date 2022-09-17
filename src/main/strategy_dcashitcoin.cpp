/*
 * strategy_constantstep.cpp
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#include "strategy_dcashitcoin.h"

#include <cmath>
#include "../imtjson/src/imtjson/object.h"
#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"

#include "sgn.h"
using json::Value;

std::string_view Strategy_DcaShitcoin::id = "dcashitcoin";

class Strategy_DcaShitcoin::IntTable {
public:
    IntTable();
    double getValue(double x) const;
    static const IntTable &getInstance();
    
    static double baseFn(double x);
protected:
    static const std::size_t _table_size = 140000;
    
    std::array<double, _table_size> _table;
    
    
};

const Strategy_DcaShitcoin::IntTable &Strategy_DcaShitcoin::IntTable::getInstance() {
    static IntTable inst;
    return inst;
}

Strategy_DcaShitcoin::IntTable::IntTable() {
    double end_range = (_table_size-1)/10000.0;
    double fna = baseFn(0);
    double res = 0;
    double a= 0;
    double b = end_range;
    double ia = a;
    for (unsigned int i = 0; i < _table_size; i++) {
        double ib = a+(b-a)*(i+1)/_table_size;
        double fnb = baseFn(ib);
        double fnc = baseFn((2*ia+ib)/3.0);
        double fnd = baseFn((ia+2*ib)/3.0);
        double r = (ib - ia)*(fna+3*fnc+3*fnd+fnb)/8.0;
        ia = ib;
        fna = fnb;
        res += r;
        _table[i] = res;
    }    
}

double Strategy_DcaShitcoin::IntTable::getValue(double x) const {
    double fpos = x*10000.0;
    double fint;
    double frac = std::modf(fpos, &fint);
    double a = 0;
    double b = 0;
    std::size_t pos = static_cast<std::size_t>(fint);
    if (pos<0) return 0;
    if (pos == _table_size) return _table[_table_size-1];
    if (pos == 0) {
        b = _table[pos];
    } else {
        a = _table[pos-1];
        b = _table[pos];
    }
    return a + (b - a) * frac;
}

double Strategy_DcaShitcoin::IntTable::baseFn(double x) {
    return std::exp(4*(1-x))*4/(std::exp(4)-1)*std::exp(-std::pow(x,10));
}

Strategy_DcaShitcoin::Strategy_DcaShitcoin(const Config &cfg, State &&st)
	:cfg(cfg),st(std::move(st))
{
}

Strategy_DcaShitcoin::Strategy_DcaShitcoin(const Config &cfg)
	:cfg(cfg)
{
}

bool Strategy_DcaShitcoin::isValid() const {
	return st.k > 0 && st.w > 0 && st.p>0;
}

PStrategy Strategy_DcaShitcoin::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		return init(!minfo.leverage,ticker.last, assets,currency);
	}
}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_DcaShitcoin::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

    State nst = st;    
    if (tradePrice > nst.k && tradePrice<st.p) {
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
        new Strategy_DcaShitcoin(cfg, std::move(nst))
    };
}

json::Value Strategy_DcaShitcoin::exportState() const {
	return json::Object({
		{"p",st.p},
		{"w",st.w},
		{"k",st.k}
	});
}



PStrategy Strategy_DcaShitcoin::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	State newst {
		src["k"].getNumber(),
		src["w"].getNumber(),
		src["p"].getNumber()
	};
	return new Strategy_DcaShitcoin(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_DcaShitcoin::getNewOrder(const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets,
		double currency, bool rej) const {
    double pos = calcPos(st.k, st.w, new_price);
    double diff = pos - assets;
    return {0, diff, pos <= minfo.calcMinSize(new_price)?Alert::forced:Alert::enabled};
}

IStrategy::MinMax Strategy_DcaShitcoin::calcSafeRange(const IStockApi::MarketInfo &minfo,
		double assets, double currencies) const {
    double pos = calcPos(st.k, st.w, st.p);
    double max;
    double minsz = minfo.calcMinSize(st.p);
    if (pos > assets+minsz) {
        max = calcPosInv(st.k, st.w, pos - assets);
    } else {
        max = calcPosInv(st.k, st.w, minsz);
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

double Strategy_DcaShitcoin::getEquilibrium(double assets) const {
    return calcPosInv(st.k, st.w, assets);
}

PStrategy Strategy_DcaShitcoin::reset() const {
	return new Strategy_DcaShitcoin(cfg,{});
}

std::string_view Strategy_DcaShitcoin::getID() const {
	return id;
}

json::Value Strategy_DcaShitcoin::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {
    
    auto pos = [&](double x) {return (minfo.invert_price?-1:1)* x;};
//    auto price = [&](double x) {return (minfo.invert_price?1/x:x);};
    
    return json::Object{
        {"Current equity", calcBudget(st.k,st.w, st.p)},
        {"Max equity", st.w},
        {"Current position", pos(calcPos(st.k, st.w, st.p))},
    };    
}

PStrategy Strategy_DcaShitcoin::init(bool spot,double price, double assets, double cur) const {
    double equity = (spot?price*assets:0) + cur;
    double ratio = assets * price / equity;
    if (ratio >=1.0) throw std::runtime_error("Can't initialize strategy with zero currency or with leverage above 1x");    
    double k = calcRatio(1, 1)>ratio?price:calcRatioInv(price, ratio);
    double b = calcBudget(k, 1, price);
    double w = equity/b;
    
    State nst;
    nst.k = k;
    nst.p = price;
    nst.w = w;
    PStrategy s = new Strategy_DcaShitcoin(cfg, std::move(nst));
    if (s->isValid()) {
        return s;
    } else {
        throw std::runtime_error("Failed to initialize strategy");
    }
}

double Strategy_DcaShitcoin::calcInitialPosition(const IStockApi::MarketInfo &minfo , double price, double assets, double currency) const {
    double eq = (minfo.leverage?0:price*assets)+currency;
    double dv = calcBudget(price, 1, price);
    double w = eq/dv;
    return calcPos(price, w, price);    
}

IStrategy::BudgetInfo Strategy_DcaShitcoin::getBudgetInfo() const {
	return BudgetInfo {
		calcBudget(st.k, st.w, st.k),
		0
	};
}


double Strategy_DcaShitcoin::calcCurrencyAllocation(double price, bool leveraged) const {
	if (leveraged) calcBudget(st.k, st.p, price);
    return calcCur(st.k, st.w, st.p);
}

Strategy_DcaShitcoin::ChartPoint Strategy_DcaShitcoin::calcChart(double price) const {
	return {
		true,
		calcPos(st.k, st.w, price),
		calcBudget(st.k, st.w, price)
	};
}

double Strategy_DcaShitcoin::calcPos(double k, double w, double price) {
    return IntTable::baseFn(price/k)*w/k;
}

double Strategy_DcaShitcoin::calcBudget(double k, double w, double price) {
    return IntTable::getInstance().getValue(price/k)*w;
}

double Strategy_DcaShitcoin::calcPosInv(double k, double w, double pos) {    
    double x = numeric_search_r1(k*1.5, [&](double x){
        return calcPos(k, w, x) - pos;
    });
    if (x<1e-100) return k*1.5;
    return x ;        
}

/*double Strategy_DcaShitcoin::calcBudgetInv(double k, double w, double budget) {
    if (budget > w) return k;
    return (k * w - std::sqrt(pow2(k) * w * (w - budget)))/w;
}*/

double Strategy_DcaShitcoin::calcCur(double k, double w, double price) {
    return calcBudget(k, w, price)-calcPos(k, w, price)*price;
}

double Strategy_DcaShitcoin::calcCurInv(double k, double w, double cur) {    
    double x = numeric_search_r1(2*k, [&](double x){
        return calcCur(k, w, x) - cur;
    });
    return x;        

}

double Strategy_DcaShitcoin::calcRatio(double k,  double price) {
    return calcPos(k, 1, price)*price/calcBudget(k, 1, price);
}

double Strategy_DcaShitcoin::calcRatioInv(double x, double ratio) {
    double k =numeric_search_r2(x,[&](double k){
        return calcRatio(k,  x) - ratio;
    });
    return k;
}
