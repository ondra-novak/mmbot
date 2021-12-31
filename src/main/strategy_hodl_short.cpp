/*
 * strategy_pile.cpp
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#include <cmath>
#include <imtjson/object.h>
#include "strategy_hodl_short.h"

#include "numerical.h"
Strategy_Hodl_Short::Strategy_Hodl_Short(const Config &cfg):cfg(cfg) {}
Strategy_Hodl_Short::Strategy_Hodl_Short(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Hodl_Short::id = "hodlshort";

bool Strategy_Hodl_Short::isValid() const {
	return st.k > 0 && st.w > 0 && st.lastp > 0;
}

double Strategy_Hodl_Short::calcAssets(double k, double w, double z, double x) {
	return w*std::exp(z*(1-x/k));
}
double Strategy_Hodl_Short::calcBudget(double k, double w, double z, double x) {
	return w*(k - std::exp(z - (z*x)/k)*k)/z;
}
double Strategy_Hodl_Short::calcFiat(double k, double w, double z, double x) {
	return calcBudget(k, w, z, x)-calcAssets(k, w, z, x)*x+k*w;
}
double Strategy_Hodl_Short::calcPriceFromAssets(double k, double w, double z, double a) {
	return k - (k *std::log(a/w))/z;
}
double Strategy_Hodl_Short::calcKFromAssets(double w, double z, double a, double x) {
	return (x * z)/(z + std::log(w/a));
}

PStrategy Strategy_Hodl_Short::init(double price, double assets, double currency, bool leveraged) const {

	double budget = leveraged?currency:(price * assets + currency);
	double ratio = assets*price / budget;
	if (ratio > 1.0) ratio = 1.0;
	if (ratio < 1e-4) ratio = 1e-4;

	double k;

	if (ratio == 1.0) {
		k = price;
	} else {
		k = numeric_search_r1(price, [&](double k){
			double a = calcAssets(k, 1, cfg.z, price);
			double f = calcBudget(k, 1, cfg.z, price);
			double b = f+k;
			return (a*price/b) - ratio;
		});
	}

	State st;
	st.w = assets/calcAssets(k, 1, cfg.z, price);
	st.k = k;
	st.lastp = price;
	st.a = assets;
	st.val = calcFiat(k, st.w, cfg.z, price);
	st.accm = 0;
	PStrategy out(new Strategy_Hodl_Short(cfg, std::move(st)));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

double Strategy_Hodl_Short::calcNewK(double new_price, double step) const {

	if (new_price > st.lastp) return st.k;
	if (new_price < st.k) return st.k;
	double lk = st.k;
	double hk = (new_price+st.k)*0.5;
	for (int i = 0; i < 50; i++) {
		double m = (lk+hk)*0.5;
		double newpos = calcAssets(m, st.w, cfg.z, new_price);
		double newdif = newpos - st.a;
		double newval = std::max(calcFiat(m, st.w, cfg.z, new_price),0.0);
		double np = (st.val - newval) - newdif * new_price+st.accm;
		if (np > 0) lk = m;
		else if (np<0) hk=m;
		else hk = lk = m;
	}
	double newk = (hk+lk)*0.5;
	return newk;
}


IStrategy::OrderData Strategy_Hodl_Short::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	if (new_price < st.k) {
		double diff = st.w - assets;
		double accm = std::max(0.0,st.accm)*cfg.acc;
		diff+=accm/new_price;
		return {0,diff,Alert::forced};
	}

	double newk = calcNewK(new_price,minfo.asset_step);
	double newa = calcAssets(newk, st.w, cfg.z, new_price);
	double diff = newa - assets;
	return {0,diff};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Hodl_Short::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	if (!isValid()) return
			this->init(tradePrice, assetsLeft-tradeSize, currencyLeft, minfo.leverage != 0)
				 ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double newk = tradePrice<st.k?tradePrice:calcNewK(tradePrice,minfo.asset_step);
	double newval = std::max(calcFiat(newk, st.w, cfg.z, tradePrice),0.0);
	double np = (st.val - newval) - tradePrice*tradeSize;
	double accm = st.accm+np;
	double a = 0;
	double nw = st.w;

	if (tradePrice < st.k) {
		np = accm;
		a = np*cfg.acc/tradePrice;
		if (cfg.reinvest) {
			nw = nw + a;
			a = 0;
		} else {
			np = np * (1-cfg.acc);
		}
		accm = 0;

	} else {
		np = 0;
	}




/*	int cnt = 0;
	while (np<0 && cnt<20) {
		newk = newk * 0.9 + st.k*0.1;
		newval = std::max(calcFiat(newk, st.w, cfg.z, tradePrice),0.0);
		np = (st.val - newval) - tradePrice*tradeSize;
		cnt++;
	}*/

	State nst;
	nst.a = assetsLeft;
	nst.k = newk;
	nst.lastp = tradePrice;
	nst.val = newval;
	nst.w = nw;
	nst.accm = accm;


	return {
		{np,a,nst.k},
		PStrategy(new Strategy_Hodl_Short(cfg, std::move(nst)))
	};

}

PStrategy Strategy_Hodl_Short::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State st {
			src["w"].getNumber(),
			src["k"].getNumber(),
			src["lastp"].getNumber(),
			src["a"].getNumber(),
			src["val"].getNumber(),

	};
	return new Strategy_Hodl_Short(cfg, std::move(st));
}

IStrategy::MinMax Strategy_Hodl_Short::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	double asst = calcAssets(st.k, st.w,cfg.z, st.lastp);
	double mx;
	if (assets >= asst*0.999) {
		mx = calcPriceFromAssets(st.k, st.w, cfg.z, st.w*0.00001);
	} else {
		mx = calcPriceFromAssets(st.k, st.w, cfg.z, asst-assets);
	}
	return {st.k, mx};
}


json::Value Strategy_Hodl_Short::exportState() const {
	return json::Object {
		{"w", st.w},
		{"k",st.k},
		{"a",st.a},
		{"val",st.val},
		{"lastp",st.lastp}
	};
}

std::string_view Strategy_Hodl_Short::getID() const {
	return id;
}

double Strategy_Hodl_Short::getCenterPrice(double lastPrice, double assets) const {
	return getEquilibrium(assets);
}

double Strategy_Hodl_Short::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(currency+price*assets);
	return budget/(price*1.01);
}

IStrategy::BudgetInfo Strategy_Hodl_Short::getBudgetInfo() const {
	return {
		calcBudget(st.k, st.w, cfg.z, st.lastp)+calcAssets(st.k, st.w, cfg.z, st.lastp)*st.lastp,
		calcAssets(st.k, st.w, cfg.z, st.lastp)
	};
}

double Strategy_Hodl_Short::getEquilibrium(double assets) const {
	return calcPriceFromAssets(st.k, st.w, cfg.z, assets);
}

double Strategy_Hodl_Short::calcCurrencyAllocation(double ) const {
	return calcFiat(st.k, st.w, cfg.z, st.lastp);

}

IStrategy::ChartPoint Strategy_Hodl_Short::calcChart(double price) const {
	return ChartPoint{
		true,
		calcAssets(st.k, st.w, cfg.z, price),
		calcBudget(st.k, st.w, cfg.z, price)
	};
}

PStrategy Strategy_Hodl_Short::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Hodl_Short::reset() const {
	return new Strategy_Hodl_Short(cfg);
}

json::Value Strategy_Hodl_Short::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	double pos = calcAssets(st.k, st.w, cfg.z, st.lastp);
	double price = st.lastp;
	if (minfo.invert_price) {
		price = 1.0/price;
		pos = -pos;
	}
	return json::Object{
		{"Currency",st.val},
		{"Last price",price},
		{"Position",pos},
		{"Max position",st.w},
		{"Accm",st.accm},
		{"Neutral Price",st.k}
	};
}

