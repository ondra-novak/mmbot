/*
 * strategy_incvalue.cpp
 *
 *  Created on: 2. 2. 2022
 *      Author: ondra
 */

#include "strategy_incvalue.h"

#include <cmath>

#include "../imtjson/src/imtjson/object.h"
#include "numerical.h"
std::string_view Strategy_IncValue::id = "inc_value";

Strategy_IncValue::Strategy_IncValue(const Config &cfg):cfg(cfg) {

}
Strategy_IncValue::Strategy_IncValue(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {

}

IStrategy::OrderData Strategy_IncValue::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	if (cfg.ms && dir>0) {
		double eq = getEquilibrium(assets);
		if (std::isfinite(eq)) {
			double s = std::min(1.0-new_price/eq,cfg.ms);
			new_price = eq * (1-s);
		}
	}
	if (assets<minfo.asset_step) assets = 0;
	double pnl = assets*(new_price - st.p);
	double newk = calc_newk(pnl, new_price);
	double newpos = cfg.fn.pos(calcW(cfg.w, st.b, newk),newk,new_price);
	if (newpos <=0) {
		return {0,-assets,Alert::forced};
	} else {
		return {0,newpos-assets,Alert::enabled};
	}
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_IncValue::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) {
		return init(!minfo.leverage, tradePrice, assetsLeft-tradeSize, currencyLeft)->onTrade(minfo,tradePrice,tradeSize,assetsLeft,currencyLeft);
	}

	if (assetsLeft<minfo.asset_step) assetsLeft = 0;
	double ppos = assetsLeft-tradeSize;
	double pnl = ppos * (tradePrice - st.p);
	double newk = tradePrice > st.k?tradePrice:
			tradeSize==0 && assetsLeft*tradePrice<currencyLeft?st.k:
					calc_newk(pnl, tradePrice);
	if (tradeSize==0 && newk<st.k) {
		newk=(newk+st.k)/2.0;
	}
	double nb = cfg.fn.budget(calcW(cfg.w, st.b, newk), newk, tradePrice);
	double np = st.v + pnl-nb;

	State nst;
	nst.spot = !minfo.leverage;
	nst.b = st.b+(cfg.reinvest?np:0);
	nst.k = newk;
	nst.v = nb;
	nst.p = tradePrice;

	return {
		{np,0,nst.k,0},
		PStrategy(new Strategy_IncValue(cfg,std::move(nst)))
	};


}

PStrategy Strategy_IncValue::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	State nst {
		!minfo.leverage,
		src["k"].getNumber(),
		src["v"].getNumber(),
		src["p"].getNumber(),
		src["b"].getNumber()
	};
	return new Strategy_IncValue(cfg, std::move(nst));
}

IStrategy::MinMax Strategy_IncValue::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {
	double a = cfg.fn.pos(calcW(), st.k, st.p);
	MinMax mx;
	if (a <= assets) {
		mx.max = st.k;
	} else {
		mx.max = cfg.fn.root(calcW(), st.k, a-assets);
	}

	if (st.spot) {
		double cc = calcCurrencyAllocation(st.p);
		mx.min = numeric_search_r1(st.k, [&](double x){
			return cfg.fn.currency(calcW(), st.k, x) + st.b + currencies - cc;
		});
	} else {
		double cc = cfg.fn.budget(calcW(), st.k, st.p) + st.b;
		mx.min = numeric_search_r1(st.k, [&](double x){
			return cfg.fn.budget(calcW(), st.k, x) + st.b + currencies - cc;
		});
	}
	return mx;

}

bool Strategy_IncValue::isValid() const {
	return st.k > 0 && st.p > 0;
}

json::Value Strategy_IncValue::exportState() const {
	return json::Value(json::object,{
			json::Value("k",st.k),
			json::Value("v",st.v),
			json::Value("p",st.p),
			json::Value("b",st.b),
	});
}

std::string_view Strategy_IncValue::getID() const {
	return id;
}

double Strategy_IncValue::getCenterPrice(double lastPrice,double assets) const {
	return getEquilibrium(assets);
}

double Strategy_IncValue::calcInitialPosition(const IStockApi::MarketInfo &, double , double ,double ) const {
	return 0;
}

IStrategy::BudgetInfo Strategy_IncValue::getBudgetInfo() const {
	return {
		cfg.fn.budget(calcW(), st.k, st.p)+st.b,
		cfg.fn.pos(calcW(), st.k, st.p),
	};
}

double Strategy_IncValue::getEquilibrium(double assets) const {
	return cfg.fn.root(calcW(), st.k, assets);
}

double Strategy_IncValue::calcCurrencyAllocation(double p) const {
	if (st.spot) return cfg.fn.currency(calcW(), st.k, st.p)+st.b;
	else return cfg.fn.budget(calcW(), st.k, p)+st.b;
}

IStrategy::ChartPoint Strategy_IncValue::calcChart(double price) const {
	if (price>st.k) return {false};
	else return {
		true,
		cfg.fn.pos(calcW(), st.k, price),
		cfg.fn.budget(calcW(), st.k, price)+st.b
	};
}

PStrategy Strategy_IncValue::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(!minfo.leverage, curTicker.last, assets, currency);
	else return this;
}

PStrategy Strategy_IncValue::reset() const {
	return new Strategy_IncValue(cfg);
}





json::Value Strategy_IncValue::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object {
		{"Price.neutral",minfo.invert_price?1.0/st.k:st.k},
		{"Price.last",minfo.invert_price?1.0/st.p:st.p},
		{"Budget.max",st.b},
		{"Budget.current",st.b+st.v},
		{"Position",cfg.fn.pos(calcW(), st.k, st.p)*(minfo.invert_price?-1.0:1.0)}
	};
}

PStrategy  Strategy_IncValue::init(bool spot, double price, double assets, double currency) const {


	State nst;
	double b = (spot?price*assets:0)+currency;
	double ratio = (assets * price)/b;
	double k = numeric_search_r2(price, [&](double k){
		double b = std::max(cfg.fn.budget(calcW(cfg.w, 1, k), k, price)+1,0.0);
		if (b == 0) return std::numeric_limits<double>::max();
		double a = cfg.fn.pos(calcW(cfg.w, 1, k), k, price);
		double r = a*price/b;
		return r - ratio;
	});

	if (k > 1e200) k = price;

	double nb = cfg.fn.budget(calcW(cfg.w, 1, k), k, price)+1;
	//x + v*x = b
	//x*(1+v) = b
	//x = b/(1+v)

	nst.b = b/nb;
	nst.v = cfg.fn.budget(calcW(cfg.w, nst.b, k), k, price);
	nst.k = k;
	nst.p = price;
	nst.spot = spot;
	PStrategy s ( new Strategy_IncValue (cfg, std::move(nst)));
	if (!s->isValid()) throw std::runtime_error("Unable to initialize strategy");
	return s;
}

double Strategy_IncValue::calc_newk(double pnl, double new_price) const {
	if (pnl == 0) return st.k;
	double nb = st.v + pnl;
	double sprd = (new_price/st.p);
	double refp = st.k*sprd;
	double yield = cfg.fn.budget(calcW(), st.k, refp);
	double f;
		f = 1-cfg.r;
	nb+=yield*f;
	if (nb > 0) return new_price;
	double k = numeric_search_r2(new_price, [&](double k){
		return cfg.fn.budget(calcW(cfg.w,st.b,k), k, new_price)-nb;
	});
	if (k>st.k) return st.k;
	return k;
}

double Strategy_IncValue::calcW() const {
	return calcW(cfg.w, st.b, st.k);
}

double Strategy_IncValue::calcW(double w, double b, double k) {
	return w * b/k;
}

double Strategy_IncValue::Function::pos(double w, double k, double x) const {
	return w*std::pow(k,z-1)*(k - x)/std::pow(x,z);
}

double Strategy_IncValue::Function::budget(double w, double k, double x) const {
	if (z == 1) return k*w*std::log(x/k)+w*(k-x);
	if (z == 2) return w*k*(1 - k/x + std::log(k/x));
	else return w*std::pow(k,z-1)*(std::pow(x,1-z)*(x/(z-2)-k/(z-1))-std::pow(k,1-z)*(k/(z-2)-k/(z-1)));
}

double Strategy_IncValue::Function::currency(double w, double k, double x) const {
	double b = budget(w,k,x);
	double v = pos(w,k,x)*x;
	return b - v;
}

double Strategy_IncValue::Function::root(double w, double k, double a) const {
	if (z == 1)  return (k * w)/(a + w);
	else return a>0.0?numeric_search_r1(k, [&](double x){
							return pos(w,k,x) - a;
					}):k;
}

