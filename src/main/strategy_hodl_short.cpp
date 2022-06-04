/*
 * strategy_pile.cpp
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#include <cmath>
#include <imtjson/object.h>
#include "strategy_hodl_short.h"

#include "sgn.h"
#include "numerical.h"
Strategy_Hodl_Short::Strategy_Hodl_Short(const Config &cfg):cfg(cfg) {}
Strategy_Hodl_Short::Strategy_Hodl_Short(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Hodl_Short::id = "hodlshort";

bool Strategy_Hodl_Short::isValid() const {
	return st.k > 0 && st.w > 0 && st.lastp > 0;
}

double Strategy_Hodl_Short::calcAssets(double k, double w, double z, double x) {
	return w*std::pow(k/x,z);
}
double Strategy_Hodl_Short::calcBudget(double k, double w, double z, double x) {
	if (std::abs(z-1)<0.00001) return k*w*std::log(x/k);
	return w*(k - std::pow(k/x,z)*x)/(z-1);
}
double Strategy_Hodl_Short::calcFiat(double k, double w, double z, double x) {
	return calcBudget(k, w, z, x)-calcAssets(k, w, z, x)*x+k*w;
}
double Strategy_Hodl_Short::calcPriceFromAssets(double k, double w, double z, double a) {
	return k * std::pow((a/w),(-1.0/z));
}
double Strategy_Hodl_Short::calcKFromAssets(double w, double z, double a, double x) {
	return x * std::pow((a/w),(1/z));
}
double Strategy_Hodl_Short::calcKFromCurrency(double w, double z, double c, double x) {
	//find maximum (because curev is convex)
	double maxk = numeric_search_r1(x, [&](double k){
		return calcFiat(k, w, z, x)-calcFiat(k*1.001, w, z, x);
	});
	//find value at maximum
	double maxv = calcFiat(maxk, w, z, x);
	//if more fiat is available, then maxk stays on maxk
	if (c > maxv) return maxk;

	double k = numeric_search_r1(x, [&](double k){
		double v = k<maxk?maxv*2:calcFiat(k, w, z, x);
		return v - c;
	});
	return k;
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


static double roundDown(double v, double step) {
	return floor(v/step)*step;
}

double Strategy_Hodl_Short::calcNewK(double new_price, double step) const {

	if (new_price > st.lastp) return st.k;
	if ( new_price * st.w*0.001>st.accm) return st.k; //this helps a little to collect more normalized profit
	if (new_price < st.k) return st.k;
	double lk = st.k;
	double hk = (new_price+st.k)*0.5;
	double np;
	double refval = std::max(calcFiat(st.k, st.w, cfg.z, new_price),0.0);
	for (int i = 0; i < 50; i++) {
		double m = (lk+hk)*0.5;
		double newpos = calcAssets(m, st.w, cfg.z, new_price);
		double newdif = roundDown(newpos - st.a, step);
		double newval = std::max(calcFiat(m, st.w, cfg.z, new_price),0.0);
		double rnp = refval + (newval - refval)*cfg.b;
		np = (st.val - rnp) - newdif * new_price+st.accm;
		if (np > 0) lk = m;
		else hk=m;
	}
	double newk = (hk+lk)*0.5;
	return newk;
}

double Strategy_Hodl_Short::calcNewA(double new_price, double dir) const {
	double newa = calcAssets(st.k, st.w, cfg.z, new_price);
	newa = std::min(st.w,newa);
	if (dir > 0 && st.accm > st.w * new_price * 0.001 && cfg.b) {
		double mina = st.a;
		double maxa = st.w;
		double acc = st.accm;
		auto calcVNNP = [&](double a) {
			double nk = calcKFromAssets(st.w, cfg.z, a, new_price);
			double newval = calcFiat(nk, st.w, cfg.z, new_price);
			double vnnp = (st.val - newval - new_price*(a-st.a))*cfg.b;
			return vnnp+acc;
		};
		double l = st.a;
		double h=st.w;
		double m;
		for (int i = 0; i < 25; i++) {
				 m = (l + h) * 0.5;
				double vnnp = calcVNNP(m)-calcVNNP(m*1.001);
				if (vnnp < 0) {
					h = m;
				}
				else if (vnnp> 0) {
					l=m;
				}
			}

		if (m>st.a) maxa=m;
		else maxa=st.w;
		double v_mina = calcVNNP(mina);
		double v_maxa = calcVNNP(maxa);
		if (v_mina * v_maxa > 0) {
				return newa;
		}
		double mlt = sgn(v_mina-v_maxa);
		for (int i = 0; i < 25; i++) {
			 m = (mina + maxa) * 0.5;
			double vnnp = calcVNNP(m);
			if (vnnp > std::max({v_maxa, v_mina})) {
				return newa;
			} else if (vnnp * mlt < 0) {
				maxa = m;
				v_maxa = vnnp;
			}
			else if (vnnp * mlt > 0) {
				mina = m;
				v_mina = vnnp;
			}
		}

		double extra = maxa == st.w?maxa:(mina + maxa) * 0.5;
		newa = newa + (extra-newa) * cfg.b;
	}
	return std::min(st.w,newa);
}

IStrategy::OrderData Strategy_Hodl_Short::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {


	double newa = calcNewA(new_price, dir);
	double minpos = std::max({minfo.asset_step, minfo.min_size, minfo.min_volume/new_price});
	if (newa < minpos) {
		if (dir<0) newa = 0; else newa =minpos;
	}
	double diff = newa - assets;
	return {0,diff, newa == st.w?Alert::forced:Alert::enabled};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Hodl_Short::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	if (!isValid()) return
			this->init(tradePrice, assetsLeft-tradeSize, currencyLeft, minfo.leverage != 0)
				 ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double newa = calcNewA(tradePrice, tradeSize);
	double unprocessed = (assetsLeft - newa)*tradePrice;
	double accm = st.accm;
	double newk =  calcKFromAssets(st.w, cfg.z, newa, tradePrice);
	double np = 0;
	double newval = calcFiat(newk, st.w, cfg.z, tradePrice);
	double vnnp = st.val - newval - tradePrice*(newa-st.a);
	double nnp = st.val - newval - tradePrice*tradeSize;
	double nw = st.w;
	double a = 0;
	double f = cfg.b;
	accm = accm + vnnp * f;
	bool achieved = newa == st.w && std::abs(assetsLeft-st.w)<std::max({minfo.asset_step,minfo.min_size,minfo.min_volume/tradePrice});
	if (achieved) {
		accm = 0;
	}
	np = np + nnp+(st.accm-accm);

	if (tradeSize == 0 && achieved) {
		np = 0;
	}

	a = np*cfg.acc/tradePrice;
	if (cfg.reinvest) {
		nw = nw + a;
		a = 0;
	} else {
		np = np * (1-cfg.acc);
	}



/*	int cnt = 0;
	while (np<0 && cnt<20) {
		newk = newk * 0.9 + st.k*0.1;
		newval = std::max(calcFiat(newk, st.w, cfg.z, tradePrice),0.0);
		np = (st.val - newval) - tradePrice*tradeSize;
		cnt++;
	}*/

	State nst;
	nst.a = newa;
	nst.k = newk;
	nst.lastp = tradePrice;
	nst.val = newval;
	nst.w = nw;
	nst.accm = accm;
	nst.uv = unprocessed;


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
			src["acm"].getNumber(),

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
		{"lastp",st.lastp},
		{"acm",st.accm}
	};
}

std::string_view Strategy_Hodl_Short::getID() const {
	return id;
}

double Strategy_Hodl_Short::getCenterPrice(double lastPrice, double assets) const {
	if (assets == 0) return lastPrice;
	return getEquilibrium(assets);
}

double Strategy_Hodl_Short::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(currency+price*assets);
	return budget/(price*1.01);
}

IStrategy::BudgetInfo Strategy_Hodl_Short::getBudgetInfo() const {
	return {
		st.w * st.k,
		st.w
	};
}

double Strategy_Hodl_Short::getEquilibrium(double assets) const {
	return calcPriceFromAssets(st.k, st.w, cfg.z, assets);
}

double Strategy_Hodl_Short::calcCurrencyAllocation(double , bool leveraged) const {
	return calcFiat(st.k, st.w, cfg.z, st.lastp)+std::max(0.0,st.accm)-st.uv;

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

