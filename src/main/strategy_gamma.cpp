/*
 * strategy_gamma.cpp
 *
 *  Created on: 20. 5. 2021
 *      Author: ondra
 */

#include "numerical.h"
#include "strategy_gamma.h"

#include <cmath>

#include "../imtjson/src/imtjson/object.h"
const std::string_view Strategy_Gamma::id = "gamma";

static double mainFunction(double x, double z) {
	double param = -(std::pow(x,z))-0.5*std::log(x);
	return std::exp(param);
}

static double calcAssets(double k, double w, double x, double z) {
	return mainFunction(x/k,z)*w/k;
}
static double calcBudget(double k, double w, double x, Strategy_Gamma::IntegrationTable &table) {
	return table.get(x/k)*w;
}


Strategy_Gamma::Strategy_Gamma(const Config &cfg):cfg(cfg) {
}

Strategy_Gamma::Strategy_Gamma(const Config &cfg, State &&st):cfg(cfg),state(st) {
}




IStrategy::OrderData Strategy_Gamma::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {
	double newk;
	double newPos = calculatePosition(new_price, newk);
	return {0,newPos - assets};
}

double Strategy_Gamma::calculateCurPosition() const {
	return calcAssets(state.kk, state.w, state.p, cfg.intTable->z);
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Gamma::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return init(minfo, tradePrice, assetsLeft, currencyLeft)
				.onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double new_k;
	double cur_pos = calculateCurPosition();
	double new_pos = calculatePosition(tradePrice, new_k);


	double accur = 2*std::max(std::max(minfo.asset_step, minfo.min_size), minfo.min_volume * tradePrice);

	if (tradePrice < state.k) {
		if (cur_pos<new_pos) {
			if (assetsLeft < new_pos - accur) {
				return {{0,0,state.k,0}, this};
			}
		} else if (cur_pos > new_pos) {
			if (assetsLeft > new_pos + accur) {
				return {{0,0,state.k,0}, this};
			}
		}
	}

	double new_kk = calibK(new_k);
	double bc = calcBudget(state.kk, state.w, state.p, *cfg.intTable);
	double bn = calcBudget(new_kk, state.w, tradePrice, *cfg.intTable);
	double pnl = (tradePrice - state.p)*(assetsLeft - tradeSize);
	double np = pnl - (bn - bc);

	State nwst = {
			new_k,state.w,tradePrice, new_kk
	};
	return {{np,0,new_k,0},
		PStrategy(new Strategy_Gamma(cfg, std::move(nwst)))
	};
}

PStrategy Strategy_Gamma::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	return new Strategy_Gamma(cfg, {
			src["k"].getNumber(),
			src["w"].getNumber(),
			src["p"].getNumber(),
			calibK(src["k"].getNumber())
	});
}

IStrategy::MinMax Strategy_Gamma::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {

	MinMax mmx;
	double a = calculateCurPosition();
	if (a>assets) {
		if (assets < 0) mmx.max = state.p;
		else mmx.max = numeric_search_r2(state.p, [&](double p){
			return calcAssets(state.kk, state.w, p, cfg.intTable->z) - a + assets;
		});
	} else {
		mmx.max = std::numeric_limits<double>::infinity();
	}
	double cur = calcBudget(state.kk, state.w, state.p, *cfg.intTable) - a*state.p;
	if (cur>currencies) {
		if (currencies < 0) mmx.min = state.p;
		else mmx.min = numeric_search_r1(state.p, [&](double p){
			return calcBudget(state.kk, state.w, p, *cfg.intTable) - cur + currencies;
		});
	}
	return mmx;
}

bool Strategy_Gamma::isValid() const {
	return state.k>0 && state.p >0 && state.w > 0;
}

json::Value Strategy_Gamma::exportState() const {
	return json::Object
			("k",state.k)
			("w",state.w)
			("p",state.p);
}

std::string_view Strategy_Gamma::getID() const {
	return id;
}

double Strategy_Gamma::getCenterPrice(double lastPrice, double assets) const {
	return getEquilibrium(assets);
}

double Strategy_Gamma::calcInitialPosition(const IStockApi::MarketInfo &minfo,
		double price, double assets, double currency) const {

	double budget = price * assets +currency;
	double kk = calibK(price);
	double normb = calcBudget(kk, 1, price, *cfg.intTable);
	double w = budget/normb;
	return calcAssets(kk, w, price, cfg.intTable->z);

}

IStrategy::BudgetInfo Strategy_Gamma::getBudgetInfo() const {
	return {calcBudget(state.kk, state.w, state.p, *cfg.intTable),
			calcAssets(state.kk, state.w, state.p, cfg.intTable->z)};

}

double Strategy_Gamma::getEquilibrium(double assets) const {
	double a = calcAssets(state.kk, state.w, state.p, cfg.intTable->z);
	if (assets > a) {
		return numeric_search_r1(state.p, [&](double price){
			return calcAssets(state.kk, state.w, price, cfg.intTable->z)-assets;
		});
	} else if (assets<a) {
		return numeric_search_r2(state.p, [&](double price){
			return calcAssets(state.kk, state.w, price, cfg.intTable->z)-assets;
		});
	} else {
		return state.p;
	}
}

double Strategy_Gamma::calcCurrencyAllocation(double price) const {
	return calcBudget(state.kk, state.w, price, *cfg.intTable)
			-calcAssets(state.kk, state.w, price, cfg.intTable->z)*price;
}

IStrategy::ChartPoint Strategy_Gamma::calcChart(double price) const {
	return {
		true,
		calcAssets(state.kk, state.w, price, cfg.intTable->z),
		calcBudget(state.kk, state.w, price, *cfg.intTable)
	};
}

PStrategy Strategy_Gamma::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (isValid()) return PStrategy(this);
	else return new Strategy_Gamma(init(minfo,curTicker.last, assets, currency));
}

PStrategy Strategy_Gamma::reset() const {
	return new Strategy_Gamma(cfg);
}

json::Value Strategy_Gamma::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	bool inv = minfo.invert_price;
	return json::Object
			("Position", (inv?-1.0:1.0) * calcAssets(state.kk, state.w, state.p, cfg.intTable->z))
			("Neutral price", inv?1.0/state.kk:state.kk)
			("Last price", inv?1.0/state.p:state.p)
			("Max budget", cfg.intTable->get_max()* state.w);

}

Strategy_Gamma::IntegrationTable::IntegrationTable(double z):z(z) {
	if (z <= 0.1) throw std::runtime_error("Invalid exponent value");

	//calculate maximum for integration. Since this point, the integral is flat
	//power number 16 by 1/z (for z=1, this value is 16)
	b = std::pow(16,1.0/z);
	//below certain point, the integral is equal to half-half (2*sqrt(x))
	a = std::pow(0.0001,1.0/z);
	//calculate integral for this point
	double y = 2*std::sqrt(a);
	//generate integration table between a and b.
	//maximum step is 0.00001
	//starting by y and generate x,y table
	generateIntTable([&](double x){
		return mainFunction(x, z);
	}, a, b, 0.0001, y, [&](double x,double y){
		values.push_back({x,y});
	});
}

double Strategy_Gamma::IntegrationTable::get(double x) const {
	//for values below a, use half-half aproximation (square root)
	if (x <= a) return 2*std::sqrt(x);
	else {
		//because table is ordered, use divide-half to search first  >= x;
		auto iter = std::lower_bound(values.begin(), values.end(), std::pair(x,0.0), std::less<std::pair<double,double> >());
		//for the very first record, just return the value
		if (iter == values.begin()) return iter->second;
		//if we are after end, return last value
		if (iter == values.end()) return values.back().second;
		//retrieve lower bound
		const auto &l = *(iter-1);
		//retrieve upper bound
		const auto &u = *(iter);
		//linear aproximation
		return l.second+(u.second-l.second)*(x - l.first)/(u.first - l.first);
	}
}



double Strategy_Gamma::calculatePosition(double price, double &newk) const  {
	newk = state.k;
	if (price >= state.k) {
		if (price < state.p) {
			newk = (price+state.k)*0.5;
			return calcAssets(calibK(newk), state.w, price, cfg.intTable->z);
		} else {
			//cur k will not change
			return calcAssets(state.kk, state.w, price, cfg.intTable->z);
		}
	}
	if (cfg.reduction_mode == 0 || (cfg.reduction_mode == 1 && price > state.p) || (cfg.reduction_mode == 2 && price < state.p)) {
		return calcAssets(state.kk, state.w, price, cfg.intTable->z);
	}
	//current position (calculated from last price)
	double cur_pos = calcAssets(state.kk, state.w, state.p, cfg.intTable->z);
	double pnl = cur_pos * (price - state.p);
	double bc = calcBudget(state.kk, state.w, state.p, *cfg.intTable);
	double needb = bc+pnl;
	newk = numeric_search_r1(1.5*state.k, [&](double k){
		return calcBudget(calibK(k), state.w, price, *cfg.intTable) - needb;
	});
	return calcAssets(calibK(newk), state.w, price, cfg.intTable->z);
}

Strategy_Gamma Strategy_Gamma::init(const IStockApi::MarketInfo &minfo,
	double price, double assets, double currency) const {

	double budget = assets * price + currency;
	State newst;
	if (budget<=0) throw std::runtime_error("No budget");
	newst.p = price;
	if (newst.p <= 0) throw std::runtime_error("Invalid price");
	if (assets <= 0) newst.k = price;
	else {
		double r = assets*price/budget;
		double k = price / cfg.intTable->b; //assume that k is lowest possible value;
		double a = calcAssets(calibK(k), 1, price, cfg.intTable->z);
		double b = calcBudget(calibK(k), 1, price, *cfg.intTable);
		double r0 = a/b*price;
		if (r > r0) {
			if (r  <0.5 ) {
				newst.k = numeric_search_r2(k, [&](double k){
					double a = calcAssets(calibK(k), 1, price, cfg.intTable->z);
					double b = calcBudget(calibK(k), 1, price, *cfg.intTable);
					return a/b*price - r;
				});
			} else {
				newst.k = (price / cfg.intTable->a)/calibK(1.0);
				budget = 2*assets*price;
			}
		} else {
			newst.k = price;
		}
	}
	newst.kk = calibK(newst.k);
	double w1 = calcBudget(newst.kk, 1.0, price, *cfg.intTable);
	newst.w = budget / w1;


	return Strategy_Gamma(cfg, std::move(newst));
}

double Strategy_Gamma::IntegrationTable::get_max() const {
	return values.back().second;
}

double Strategy_Gamma::calibK(double k) const {
	double l = -cfg.trend/100.0;
	double kk = std::pow(std::exp(-1.6*l*l+3.4*l),1.0/cfg.intTable->z);
	return k/kk;
}
