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

PStrategy Strategy_Hodl_Short::init(double price, double assets, double currency, bool leveraged) const {

	PStrategy out(new Strategy_Hodl_Short(cfg, {
			assets,
			price,
			price,
	}));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

double Strategy_Hodl_Short::calcNewK( double new_price) const {

	double newk = st.k;

	if (new_price < st.k) return new_price;

	double c = cfg.intTable->calcCurrency(st.k, st.w, st.lastp);
	double c2 = cfg.intTable->calcCurrency(st.k, st.w, new_price);
	double cp = cfg.intTable->calcAssets(st.k, st.w, st.lastp);
	double np = cfg.intTable->calcAssets(st.k, st.w, new_price);
	double diff = np - cp;
	double nc = c - diff * new_price;
	double extra = nc - c2;
	double extrapos = extra/new_price;
	double tpos = np+extrapos;
	double rprice = cfg.intTable->calcPrice(st.k, st.w, tpos);
	newk = new_price/(rprice/st.k);

	return newk;
}

IStrategy::OrderData Strategy_Hodl_Short::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {


	if (new_price < st.k) {
		double diff = st.w - assets;
		return {0,diff,Alert::forced};
	}
	double newk = calcNewK(new_price);
	double newa = cfg.intTable->calcAssets(newk, st.w, new_price);
	if (!minfo.leverage) newa = std::max(0.0, newa);
	double diff = newa - assets;
	return {0,diff};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Hodl_Short::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	if (!isValid()) return
			this->init(tradePrice, assetsLeft-tradeSize, currencyLeft, minfo.leverage != 0)
				 ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);



	double chg = -tradePrice * tradeSize;
	double c1 = cfg.intTable->calcCurrency(st.k, st.w, st.lastp);
//	double a2 = cfg.intTable->calcAssets(st.k, st.w, tradePrice);
	double newk = std::min(tradePrice,tradeSize?calcNewK( tradePrice):st.k);

	double c2 = cfg.intTable->calcCurrency(newk, st.w, tradePrice);
	double dff = c1+chg-c2;

	State nst = st;
	nst.k = newk;
	nst.lastp = tradePrice;


	return {
		{dff,0,nst.k},
		PStrategy(new Strategy_Hodl_Short(cfg, std::move(nst)))
	};

}

PStrategy Strategy_Hodl_Short::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State st {
			src["w"].getNumber(),
			src["k"].getNumber(),
			src["lastp"].getNumber(),

	};
	return new Strategy_Hodl_Short(cfg, std::move(st));
}

IStrategy::MinMax Strategy_Hodl_Short::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	double mx;
	if (minfo.leverage) {
		mx = numeric_search_r2(st.k, [&](double x){
			return cfg.intTable->calcBudget(st.k, st.w, x);
		});
	} else {
		mx = cfg.intTable->calcPrice(st.k, st.w, 0);
	}
	return {st.k, mx};
}

bool Strategy_Hodl_Short::isValid() const {
	return st.w>0 && st.k>0 && st.lastp>0;
}

json::Value Strategy_Hodl_Short::exportState() const {
	return json::Object {
		{"w", st.w},
		{"k",st.k},
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
		cfg.intTable->calcBudget(st.k, st.w, st.lastp),
		cfg.intTable->calcAssets(st.k, st.w, st.lastp)
	};
}

double Strategy_Hodl_Short::getEquilibrium(double assets) const {
	return cfg.intTable->calcPrice(st.k, st.w, assets);
}

double Strategy_Hodl_Short::calcCurrencyAllocation(double price) const {
	return cfg.intTable->calcCurrency(st.k, st.w, st.lastp);

}

IStrategy::ChartPoint Strategy_Hodl_Short::calcChart(double price) const {
	return ChartPoint{
		true,
		cfg.intTable->calcAssets(st.k, st.w, price),
		cfg.intTable->calcBudget(st.k, st.w, price)
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
	double pos = cfg.intTable->calcAssets(st.k, st.w, st.lastp);
	double price = st.lastp;
	if (minfo.invert_price) {
		price = 1.0/price;
		pos = -pos;
	}
	return json::Object{
		{"Budget",cfg.intTable->calcBudget(st.k, st.w, st.lastp)},
		{"Currency",cfg.intTable->calcCurrency(st.k, st.w, st.lastp)},
		{"Last price",price},
		{"Position",pos},
		{"Neutral Price",st.k}
	};
}

Strategy_Hodl_Short::IntegrationTable::IntegrationTable(double z, double wd)
:z(z),wd(wd)
{
	if (z <= 0.1) throw std::runtime_error("Invalid exponent value");


	generateIntTable([&](double x){
		return mainFunction(x);
	}, 1, 2.5/wd, 0.00001, 0, [&](double x,double y){
		values.push_back({x,y});
	});

}

double Strategy_Hodl_Short::IntegrationTable::get(double x) const {
	//because table is ordered, use divide-half to search first  >= x;
	auto iter = std::lower_bound(values.begin(), values.end(), std::pair(x,0.0), std::less<std::pair<double,double> >());
	//for the very first record, just return the value
	if (iter == values.begin()) {
		if (iter != values.end()) ++iter;
		else return iter->second;
	}
	//if we are after end, return last value
	if (iter == values.end()) {
		iter--;
	}
	//retrieve lower bound
	const auto &l = *(iter-1);
	//retrieve upper bound
	const auto &u = *(iter);
	//linear aproximation
	return l.second+(u.second-l.second)*(x - l.first)/(u.first - l.first);
}

double Strategy_Hodl_Short::IntegrationTable::mainFunction(double x) const {
	//defined for x>1 and y > -10
	double y = std::max(-10.0,2.0-std::exp(std::pow(wd*(std::max(1.0,x)-1),z))); //don't go to low numbers, 10x leverage is too high for spot strategy
	return y;
}

double Strategy_Hodl_Short::IntegrationTable::calcAssets(double k, double w, double x) const {
	double y = mainFunction(x/k)*w;
	return y;
}

double Strategy_Hodl_Short::IntegrationTable::calcCurrency(double k, double w,double x) const {
	return calcBudget(k, w, x) - calcAssets(k, w, x) * x;
}

double Strategy_Hodl_Short::IntegrationTable::inv(double a) const {
	double x = (wd + std::pow(std::log(2 - a),(1/z)))/wd;
	return x;
}

double Strategy_Hodl_Short::IntegrationTable::calcPrice(double k, double w, double a) const {
	double x = inv(a/w)*k;
	return x;
}

double Strategy_Hodl_Short::IntegrationTable::calcBudget(double k, double w, double x) const {
	double y = get(x/k)*w*k+k*w;
	return y;
}
