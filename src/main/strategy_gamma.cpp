/*
 * strategy_gamma.cpp
 *
 *  Created on: 20. 5. 2021
 *      Author: ondra
 */

#include "numerical.h"
#include "strategy_gamma.h"

#include <cmath>

#include <imtjson/object.h>
#include <imtjson/string.h>
#include <shared/logOutput.h>
#include "sgn.h"

using ondra_shared::logDebug;
const std::string_view Strategy_Gamma::id = "gamma";



Strategy_Gamma::Strategy_Gamma(const Config &cfg):cfg(cfg) {
}

Strategy_Gamma::Strategy_Gamma(const Config &cfg, State &&st):cfg(cfg),state(st) {
}

Strategy_Gamma::Strategy_Gamma(Strategy_Gamma &&other)
	:cfg(std::move(other.cfg))
	,state(std::move(other.state)) {}


static double minSize(const IStockApi::MarketInfo &minfo, double price) {
	return std::max({
		minfo.min_size,
		minfo.min_volume / price,
		minfo.asset_step
	});
}

static double roundZero(double finpos, const IStockApi::MarketInfo &minfo, double price) {
	double afinpos = std::abs(finpos);
	if (afinpos < minSize(minfo, price)) return 0;
	return finpos;
}

static double calcMinOrderSize(const IStockApi::MarketInfo &minfo, double price) {
	return std::max({minfo.asset_step, minfo.min_size, minfo.min_volume/price});
}


IStrategy::OrderData Strategy_Gamma::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {
	double newPos = calculatePosition(assets,new_price, calcMinOrderSize(minfo, new_price));
	double newPosz = roundZero(newPos, minfo, new_price);
	double dff = newPosz - assets;
	double dffz = roundZero(dff, minfo, new_price);
	if (dir < 0 && dffz == 0 && newPosz == 0) {
		return {new_price,0,Alert::forced};
	} else if (dir > 0 && dffz == 0 && newPosz == 0) {
		return {0, minSize(minfo, new_price)};
	}
	return {0,dff};
}

double Strategy_Gamma::calculateCurPosition() const {
	return cfg.intTable->calcAssets(state.kk, state.w, state.p );
}


std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Gamma::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return init(minfo, tradePrice, assetsLeft, currencyLeft)
				.onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double cur_pos = assetsLeft - tradeSize;

	auto nn = calculateNewNeutral(cur_pos, tradePrice, calcMinOrderSize(minfo, tradePrice));
	if (tradeSize == 0 && std::abs(nn.k-tradePrice)>std::abs(state.k - tradePrice)) {
		nn.k = state.k;
		nn.w = state.w;

	}
	double newkk = calibK(nn.k);
	double volume = -tradePrice*tradeSize;
	double calc_pos = cfg.intTable->calcAssets(newkk, nn.w, tradePrice);
	double unprocessed = (assetsLeft - calc_pos)*tradePrice;
	double prev_calc_pos = cfg.intTable->calcAssets(state.kk, state.w, state.p);
	double prev_unprocessed = (assetsLeft-tradeSize - prev_calc_pos)*state.p;
	double prev_cur = state.b - prev_calc_pos * state.p - prev_unprocessed;
	double bn = cfg.intTable->calcBudget(newkk, nn.w, tradePrice);
	double new_cur = bn - calc_pos * tradePrice - unprocessed;
	double np = volume - new_cur + prev_cur  ;
	double neww = nn.w;
	double d = state.d;

	if (cfg.reinvest && tradeSize) {
		d = d + np;
		if (d > 0) {
			neww = nn.w * (bn+d)/bn;
			d = 0;
		}
		bn = cfg.intTable->calcBudget(newkk, neww, tradePrice);
	}

	State nwst = {
			nn.k,neww,tradePrice, bn,d,unprocessed,newkk
	};
	return {{np,0,nn.k,0},
		PStrategy(new Strategy_Gamma(cfg, std::move(nwst)))
	};
}

PStrategy Strategy_Gamma::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {

	State nwst{
		src["k"].getNumber(),
		src["w"].getNumber(),
		src["p"].getNumber(),
		src["b"].getNumber(),
		src["d"].getNumber(),
		src["uv"].getNumber()
	};
	if (src["hash"].hasValue() && cfg.calcConfigHash() != src["hash"].toString()) {
		nwst.k = 0;
		nwst.kk = 0;
	} else {
		nwst.kk = calibK(nwst.k);
	}
	return new Strategy_Gamma(cfg,std::move(nwst));
}

IStrategy::MinMax Strategy_Gamma::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {

	MinMax mmx;
	double a = calculateCurPosition();
	if (a>assets) {
		if (assets < 0) mmx.max = state.p;
		else mmx.max = numeric_search_r2(state.p, [&](double p){
			return cfg.intTable->calcAssets(state.kk, state.w, p) - a + assets;
		});
	} else {
		mmx.max = std::numeric_limits<double>::infinity();
	}
	double cur = cfg.intTable->calcBudget(state.kk, state.w, state.p) - a*state.p;
	double adjcur = minfo.leverage? (minfo.leverage*currencies  - assets * state.p):currencies;
	if (cur>adjcur || cfg.intTable->fn == keepvalue) {
		if (adjcur < 0) mmx.min = state.p;
		else mmx.min = numeric_search_r1(state.p, [&](double p){
			return cfg.intTable->calcBudget(state.kk, state.w, p)
					- cfg.intTable->calcAssets(state.kk, state.w, p)*state.p
					- cur + adjcur;
		});
	} else mmx.min = 0;
	return mmx;
}

bool Strategy_Gamma::isValid() const {
	return state.k>0 && state.p >0 && state.w > 0 && state.b > 0;
}

json::Value Strategy_Gamma::exportState() const {
	return json::Object({
		{"k",state.k},
		{"w",state.w},
		{"p",state.p},
		{"b",state.b},
		{"d",state.d},
		{"uv",state.uv},
		{"hash",cfg.calcConfigHash()}
	});
}

std::string_view Strategy_Gamma::getID() const {
	return id;
}

double Strategy_Gamma::getCenterPrice(double lastPrice, double assets) const {
	if (assets == 0) return lastPrice;
	return getEquilibrium(assets);
}

double Strategy_Gamma::calcInitialPosition(const IStockApi::MarketInfo &minfo,
		double price, double assets, double currency) const {

	double budget = price * assets +currency;
	double kk = calibK(price);
	double normb = cfg.intTable->calcBudget(kk, 1, price);
	double w = budget/normb;
	return cfg.intTable->calcAssets(kk, w, price);

}

IStrategy::BudgetInfo Strategy_Gamma::getBudgetInfo() const {
	return {cfg.intTable->calcBudget(state.kk, state.w, state.p),
		cfg.intTable->calcAssets(state.kk, state.w, state.p)};

}

double Strategy_Gamma::getEquilibrium(double assets) const {
	double a = cfg.intTable->calcAssets(state.kk, state.w, state.p);
	if (assets > a) {
		return numeric_search_r1(state.p, [&](double price){
			return cfg.intTable->calcAssets(state.kk, state.w, price)-assets;
		});
	} else if (assets<a) {
		return numeric_search_r2(state.p, [&](double price){
			return cfg.intTable->calcAssets(state.kk, state.w, price)-assets;
		});
	} else {
		return state.p;
	}
}

double Strategy_Gamma::calcCurrencyAllocation(double) const {
	return cfg.intTable->calcBudget(state.kk, state.w, state.p)
			-cfg.intTable->calcAssets(state.kk, state.w, state.p)*state.p
			-state.uv;
}

IStrategy::ChartPoint Strategy_Gamma::calcChart(double price) const {
	double a = cfg.intTable->calcAssets(state.kk, state.w, price);
	double b = cfg.intTable->calcBudget(state.kk, state.w, price);
	if (b < 0) b = 0;
	return {
		true,
		a,
		b
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
	return json::Object({
		{"Position", (inv?-1.0:1.0) * cfg.intTable->calcAssets(state.kk, state.w, state.p)},
		{"Price.neutral", inv?1.0/state.kk:state.kk},
		{"Price.last", inv?1.0/state.p:state.p},
		{"Budget.max", cfg.intTable->get_max()* state.w},
		{"Budget.current", state.b},
		{"Budget.not_traded", state.uv}
	});

}

Strategy_Gamma::IntegrationTable::IntegrationTable(Function fn, double z):fn(fn),z(z) {
	if (z <= 0.1) throw std::runtime_error("Invalid exponent value");
	double y;

	//calculate maximum for integration. Since this point, the integral is flat
	//power number 16 by 1/z (for z=1, this value is 16)
	b = std::pow(16,1.0/z);
	switch (fn){
	case exponencial:
	case gauss: a = 0;
				y=0;break;
	case invsqrtsinh: a= pow(0.001,1.0/z);;
					   y=0;break;
	case halfhalf: a=std::pow(0.0001,1.0/z);
				   y=2*std::sqrt(a);
				   break;
	default:a=std::pow(0.0001,1.0/z);
			y=0;
			break;
	}
	//generate integration table between a and b.
	//maximum step is 0.00001
	//starting by y and generate x,y table
	generateIntTable([&](double x){
		return mainFunction(x);
	}, a, b, 0.0001, y, [&](double x,double y){
		values.push_back({x,y});
	});
}

double Strategy_Gamma::IntegrationTable::get(double x) const {
	//for values below a, use half-half aproximation (square root)
	if (x <= a) {
		if (fn == halfhalf) {
			return 2*std::sqrt(x);
		} else if (fn == invsqrtsinh) {
			return values[0].second;
		} else {
			return std::log(x/a);
		}
	}
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


Strategy_Gamma::NNRes Strategy_Gamma::calculateNewNeutral(double a, double price, double min_order_size) const {
	if ((price-state.k)*(state.p - state.k) < 0) {
		return {state.k, state.w};
	}
	double pnl = a*(price - state.p);
	double w = state.w;
	int mode = cfg.reduction_mode;
	if (price < state.k && !cfg.maxrebalance  && (mode == 0
			|| (mode == 1 && price > state.p)
			|| (mode == 2 && price < state.p))) return {state.k,w};

	double bc;
	double needb;
	double newk;
	if (price > state.k) {

		if (price < state.p && cfg.maxrebalance) {
			bc = cfg.intTable->calcBudget(state.kk, state.w, state.p);
			needb = pnl+bc;
			w = numeric_search_r2(0.5*state.w, [&](double w){
				return cfg.intTable->calcBudget(state.kk, w, price) - needb;
			});
			newk = state.k;
		} else {
			bc = cfg.intTable->calcBudget(state.kk, state.w, price);
			needb = bc-pnl;
			newk = numeric_search_r2(0.5*state.k, [&](double k){
				return cfg.intTable->calcBudget(calibK(k), state.w, state.p) - needb;
			});
			if (newk<state.k /*&& cfg.intTable->calcAssets(newk, state.w, price)<min_order_size*/)
				newk = (3*state.k+price)*0.25;
		}
	} else if (price < state.k){
		if (cfg.maxrebalance && price > state.p) {
			double k = price*0.1+state.k*0.9;//*cfg.intTable->get_min();
			double kk = calibK(k);
			double w1 = cfg.intTable->calcAssets(kk, 1.0, price);
			double w2 = cfg.intTable->calcAssets(state.kk, state.w, price);
			double neww = w2/w1;
			if (neww>w*2){
				return {state.k, state.w};
			}
			w = neww;
			newk = k;
			double pos1 = cfg.intTable->calcAssets(state.kk, state.w, price);
			double pos2 = cfg.intTable->calcAssets(kk, w, price);
			double b1 = cfg.intTable->calcBudget(state.kk, state.w, price);
			double b2 = cfg.intTable->calcBudget(kk, w, price);
			logDebug("Rebalance POS: $1 > $2, BUDGET: $3 > $4", pos1, pos2, b1, b2);
		} else {
			bc = cfg.intTable->calcBudget(state.kk, state.w, state.p);
			needb = bc+pnl;
			if (mode == 4 && price/state.kk > 1.0) {
				double spr = price/state.p;
				double ref = cfg.intTable->calcAssets(state.kk, state.w, state.k)*state.k*(spr-1.0)
						+ cfg.intTable->calcBudget(state.kk, state.w, state.k)-cfg.intTable->calcBudget(state.kk, state.w, state.k*spr);
				//double bq = cfg.intTable->calcBudget(state.kk, state.w, price);
//				double maxref = needb - bq;
				//ref = std::min(ref, maxref);
				needb = needb-ref;
			}
			newk = numeric_search_r1(1.5*state.k, [&](double k){
				return cfg.intTable->calcBudget(calibK(k), state.w, price) - needb;
			});
		}
	} else {
		newk = state.k;
	}
	if (newk < 1e-100 || newk > 1e+100) newk = state.k;
	return {newk,w};

}

double Strategy_Gamma::calculatePosition(double a,double price, double minsize) const  {
	auto newk = calculateNewNeutral(a, price, minsize);
	double newkk = calibK(newk.k);
	return cfg.intTable->calcAssets(newkk, newk.w, price);
}

Strategy_Gamma Strategy_Gamma::init(const IStockApi::MarketInfo &minfo,
	double price, double assets, double currency) const {

	double budget = state.b > 0?state.b:assets * price + currency;
	State newst;
	if (budget<=0) throw std::runtime_error("No budget");
	if (state.p) price = state.p;
	newst.p = price;
	if (newst.p <= 0) throw std::runtime_error("Invalid price");
	if (assets <= 0) newst.k = price;
	else {
		double r = assets*price/budget;
		double k = price / cfg.intTable->b; //assume that k is lowest possible value;
		double a = cfg.intTable->calcAssets(calibK(k), 1, price);
		double b = cfg.intTable->calcBudget(calibK(k), 1, price);
		double r0 = a/b*price;
		if (r > r0) {
			if (r  <0.5 || cfg.intTable->fn != halfhalf) {
				newst.k = numeric_search_r2(k, [&](double k){
					double a = cfg.intTable->calcAssets(calibK(k), 1, price);
					double b = cfg.intTable->calcBudget(calibK(k), 1, price);
					if (b <= 0) return std::numeric_limits<double>::max();
					if (a <= 0) return 0.0;
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
	double w1 = cfg.intTable->calcBudget(newst.kk, 1.0, price);
	newst.w = budget / w1;
	newst.b = budget;
	newst.d = 0;
	newst.uv = 0;

	Strategy_Gamma s(cfg, std::move(newst));
	if (s.isValid()) return s;
	throw std::runtime_error("Failed to initialize strategy");
}

double Strategy_Gamma::IntegrationTable::get_max() const {
	return values.back().second;
}
double Strategy_Gamma::IntegrationTable::get_min() const {
	return std::pow(0.000095,1.0/z);
}


double Strategy_Gamma::calibK(double k) const {
	if (cfg.maxrebalance) return k/cfg.intTable->get_min();
	double l = -cfg.trend/100.0;
	double kk = std::pow(std::exp(-1.6*l*l+3.4*l),1.0/cfg.intTable->z);
	return k/kk;
}

double Strategy_Gamma::IntegrationTable::mainFunction(double x) const {
	switch (fn) {
	case halfhalf:return std::exp(-(std::pow(x,z))-0.5*std::log(x));
	case keepvalue: return std::exp(-std::pow(x,z))/x;
	case exponencial: return std::exp(-std::pow(x,z));
	case gauss: return std::exp(-pow2(x)-std::pow(x,z));
	case invsqrtsinh: return 1.0/std::sqrt(std::sinh(std::pow(x*1.2,z)));
	default : return 0;
	}
}

double Strategy_Gamma::IntegrationTable::calcAssets(double k, double w,
		double x) const {
	switch (fn) {
	case halfhalf: return mainFunction(x/k)*w/k;
	case keepvalue:  return mainFunction(x/k)*w/k;
	case exponencial:  return mainFunction(x/k)*w/k;
	case gauss:  return mainFunction(x/k)*w/k;
	case invsqrtsinh:  return mainFunction(x/k)*w/k;
	default : return 0;
	}


}

double Strategy_Gamma::IntegrationTable::calcBudget(double k, double w, double x) const {
	switch (fn) {
		case halfhalf: return get(x/k)*w;
		case keepvalue: return get(x/k)*w;
		case exponencial: return get(x/k)*w;
		case gauss: return get(x/k)*w;
		case invsqrtsinh: return get(x/k)*w;
		default : return 0;
	};

}

double Strategy_Gamma::IntegrationTable::calcCurrency(double k, double w, double x) const {
	return calcBudget(k,w,x) - calcAssets(k,w,x)*x;
}

json::String Strategy_Gamma::Config::calcConfigHash() const {
	std::hash<json::Value> h;
	return json::Value(h({(int)intTable->fn, intTable->z, trend})).toString();

}
