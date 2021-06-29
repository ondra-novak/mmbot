/*
 * strategy_sinh_gen.cpp
 *
 *  Created on: 27. 5. 2021
 *      Author: ondra
 */


#include "strategy_sinh_gen.h"
#include <cmath>

#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"
static const double INT_RANGE = 1000000;
static const double MAX_ERROR = 0.0001;


const std::string_view Strategy_Sinh_Gen::id = "sinh_gen";

bool Strategy_Sinh_Gen::FnCalc::sortPoints(const Point &a, const Point &b) {
	return a.first < b.first;
}

Strategy_Sinh_Gen::FnCalc::FnCalc(double wd)
:wd(wd) {

		auto fillFn = [&](double x, double y) {
			itable.push_back({x,y});
		};

		double a = numeric_search_r1(1, [&](double x) {
			return baseFn(x) - INT_RANGE; //should be enough (1000000x leverage)
		});
		double b = numeric_search_r2(1, [&](double x) {
			return baseFn(x) + INT_RANGE; //should be enough (1000000x leverage)
		});

		generateIntTable([&](double x){
			return baseFn(x);
		}, 1, a, MAX_ERROR, 0, fillFn);

		generateIntTable([&](double x){
			return baseFn(x);
		}, 1, b, MAX_ERROR, 0, fillFn);

		std::sort(itable.begin(),itable.end(), sortPoints);
}

double Strategy_Sinh_Gen::FnCalc::baseFn(double x) const {
	return std::sinh(wd*(1-std::sqrt(x)))/x;
}


double Strategy_Sinh_Gen::FnCalc::integralBaseFn(double x) const {
	double r;
		auto iter = std::lower_bound(itable.begin(), itable.end(), Point(x,0), sortPoints);
		Point p1,p2;
		if (iter == itable.begin()) {
			p1 = *iter;
			p2 = *(iter+1);
		} else if (iter == itable.end()) {
			p1 = itable[itable.size()-2];
			p2 = itable[itable.size()-1];
		} else {
			p1 = *(iter-1);
			p2 = *(iter);
		}
		double f = (x - p1.first)/(p2.first - p1.first);
		r = p1.second + (p2.second - p1.second) * f;
	return r;


}

double Strategy_Sinh_Gen::FnCalc::assets(double k, double w, double x) const {
	return baseFn(x/k)*w;
}

double Strategy_Sinh_Gen::FnCalc::budget(double k, double w, double x) const {
	return integralBaseFn(x/k)*w*k;
}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg) :cfg(cfg),pw(cfg.power) {}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg, State &&st) :
		cfg(cfg), st(std::move(st)){
	pw = cfg.power * st.budget/st.k;
}

bool Strategy_Sinh_Gen::isValid() const {
	return st.k>0 && st.p>0 && st.budget>0;
}

PStrategy Strategy_Sinh_Gen::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(minfo, curTicker.last, assets, currency);
	else return this;
}

PStrategy Strategy_Sinh_Gen::init(const IStockApi::MarketInfo &minfo,
		double price, double pos, double currency) const {

	double budget = minfo.leverage?currency:(pos * price + currency);
	State nwst;
	nwst.p = price;
	nwst.spot = minfo.leverage == 0;

	if (pos > 0) {
		nwst.k = numeric_search_r2(price, [&](double x) {
			return cfg.calc->assets(x, pw, price)-pos;
		});
	} else if (pos < 0) {
		nwst.k = numeric_search_r1(price, [&](double x) {
			return cfg.calc->assets(x, pw, price)-pos;
		});
	} else {
		nwst.k = price;
	}
	double pw = cfg.power * currency/nwst.k;
	nwst.budget = budget-cfg.calc->budget(nwst.k, pw, price);
	PStrategy s(new Strategy_Sinh_Gen(cfg, std::move(nwst)));
	if (!s->isValid()) throw std::runtime_error("Unable to initialize the strategy");
	return s;
}

double Strategy_Sinh_Gen::calcNewK(double tradePrice, double cb, double pnl) const {
	if (pnl == 0) return st.k;
	double newk;
	if (st.p == st.k) return st.k;
	if ((st.p<st.k && tradePrice<st.k) || (st.p>st.k && tradePrice>st.k))  {
		double sprd = cfg.avgspread && st.trades?(1.0+st.sum_spread/st.trades):(tradePrice/st.p);
		double refp = st.k*sprd;
		double refb = pnl>0?cfg.calc->budget(st.k, pw, refp):0;
//		double cnb = cfg.calc->budget(st.k, pw, tradePrice);
		double nb = cb+pnl+refb;

		if (nb > 0) {
			return tradePrice;
		}

			if (st.p < st.k) {
				newk = numeric_search_r2(tradePrice, [&](double k){
					return cfg.calc->budget(k, pw, tradePrice)-nb;
				});
				if (newk>1e300) newk = st.k;
			} else if (st.p > st.k) {
				newk = numeric_search_r1(tradePrice, [&](double k){
					return cfg.calc->budget(k, pw, tradePrice)-nb;
				});
				if (newk<1e-200) newk = st.k;
			} else {
				newk = tradePrice;
			}

			if (std::abs(st.k-tradePrice)<std::abs(newk-tradePrice))
				newk = st.k;
	} else {
		newk = tradePrice;
	}
	return newk;

}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_Sinh_Gen::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return init(minfo, tradePrice, assetsLeft, currencyLeft)
				->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double absass = std::abs(assetsLeft);
	if (absass < minfo.min_size || absass*tradePrice < minfo.min_volume || absass < (st.budget/1e8)/tradePrice) {
		assetsLeft = 0;
	}


	double prevPos = assetsLeft - tradeSize;
	double cb = cfg.calc->budget(st.k, pw, st.p);
	double pnl = prevPos*(tradePrice - st.p);
	double newk = assetsLeft?calcNewK(tradePrice, cb, pnl):tradePrice;

	double nb = cfg.calc->budget(newk, pw, tradePrice);
	double np = pnl - (nb - cb);

	State nwst;
	nwst.spot = minfo.leverage == 0;
	nwst.budget = (cfg.reinvest?np:0)+st.budget;
	nwst.k = newk;
	nwst.p = tradePrice;
	nwst.last_spread = tradePrice/st.p;
	if (nwst.last_spread<1.0) nwst.last_spread=1.0/nwst.last_spread;
	if (nwst.last_spread>1.0) {
		nwst.trades = st.trades+1;
		nwst.sum_spread = st.sum_spread + (nwst.last_spread-1.0);
	}

	return {
		OnTradeResult{np,0,newk,0},
		PStrategy(new Strategy_Sinh_Gen(cfg, std::move(nwst)))
	};
}

json::Value Strategy_Sinh_Gen::exportState() const {
	using namespace json;
	return Value(object,{
			Value("k", st.k),
			Value("p", st.p),
			Value("budget", st.budget),
			Value("last_spread", st.last_spread),
			Value("sum_spread", st.sum_spread),
			Value("trades", st.trades),
	});
}

PStrategy Strategy_Sinh_Gen::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State nwst {
		minfo.leverage == 0,
		src["k"].getNumber(),
		src["p"].getNumber(),
		src["budget"].getNumber(),
		src["last_spread"].getNumber(),
		src["sum_spread"].getNumber(),
		static_cast<int>(src["trades"].getInt())
	};
	return new Strategy_Sinh_Gen(cfg, std::move(nwst));
}

json::Value Strategy_Sinh_Gen::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	auto getpx = [&](double px){return minfo.invert_price?1.0/px:px;};
	auto getpos = [&](double pos){return minfo.invert_price?-pos:pos;};
	double sprd = cfg.avgspread && st.trades?(1.0+st.sum_spread/st.trades):st.last_spread;

	using namespace json;

	return Value(object, {
			Value("Price-last", getpx(st.p)),
			Value("Price-neutral", getpx(st.k)),
			Value("Budget-total", st.budget),
			Value("Budget-current", cfg.calc->budget(st.k, pw, st.p)+st.budget),
			Value("Position", getpos(cfg.calc->assets(st.k, pw, st.p))),
			Value("Profit per trade", -cfg.calc->budget(st.k, pw, st.k*sprd)),
			Value("Profit per trade[%]", -cfg.calc->budget(st.k, pw, st.k*sprd)/st.budget*100)
	});
}

IStrategy::OrderData Strategy_Sinh_Gen::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double absass = std::abs(assets);
	if (absass < minfo.min_size || absass*new_price < minfo.min_volume || absass < (st.budget/1e8)/new_price) {
		assets = 0;
	}

	//calculate current budget
	double cb = cfg.calc->budget(st.k, pw, st.p);
	//calculate pnl
	double pnl = assets*(new_price - st.p);
	//calculate new k for budgetr and pnl
	double newk = calcNewK(new_price, cb, pnl);
	//calculate minimal allowed budget
	//	double minbudget = st.budget*(1.0-cfg.stopOnLoss);

	//calculate new position
	double new_pos = limitPosition(cfg.calc->assets(newk, pw, new_price));
	//calculate difference between new position and curreny position
	double dfa = new_pos -assets;
	//if difference is in other direction and position should decrease
	/*if (dfa*dir < 0 && dir*(new_price-st.p)<0) {
		//calculate equilibrium
		double eq = getEquilibrium(assets);
		//calculate distance between last price and current price
		double df = new_price - st.p;
		//calculate position on shifte price
		double new_a = limitPosition(cfg.calc->assets(newk, pw, eq+df));
		//difference
		double dfa = new_a-assets;
		//if (difference is valid
		if (dfa*dir >= 0) {
			//create order with this difference
			return {0,dfa};
		}
	}*/
	//if new position is reverse or zero
	if (new_pos * assets <0 || new_pos == 0) {
		//close current position (force alert)
		return {0,-assets,Alert::forced};
	}
	//if position is increasing
	/*
	if (dfa * assets > 0 && dir * assets > 0) {
		//if reached minbugget
		if (currency<=minbudget) {
			//send alert only
			return {0,0,Alert::forced};
		}
		//calculate price difference
		double df = new_price - st.p;
		//calculate next price if difference maintain
		double next_price = new_price + df;
		//don't allow negative price
		if (next_price < 0) next_price = 0;
		//max_pos, maximum position, which causes, that on next_price, minbudget will be reached
		double max_pos = (next_price - new_price)/(minbudget-currency);

		//reached max_pos
		if (max_pos*dir > new_pos*dir) {
			double dfma = max_pos - assets;
			//negative direction?
			if (dfma * dir < 0) {
				//stop trading now
				return {0,0,Alert::forced};
			}
			new_pos = max_pos;
			dfa = dfma;
		}
	}*/
	return {0, dfa};
}

double Strategy_Sinh_Gen::limitPosition(double pos) const {
	if ((cfg.disableSide<0 || st.spot) && pos<0) return 0;
	if (cfg.disableSide>0 && pos>0) return 0;
	return pos;
}

IStrategy::MinMax Strategy_Sinh_Gen::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {

	double b = st.budget;
	MinMax ret;
	ret.min = numeric_search_r1(st.k, [&](double x){
		return cfg.calc->budget(st.k, pw, x)+b;
	});
	ret.max = numeric_search_r2(st.k, [&](double x){
		return cfg.calc->budget(st.k, pw, x)+b;
	});
	return ret;
}

double Strategy_Sinh_Gen::getEquilibrium(double assets) const {
	if (assets<0) {
		return numeric_search_r2(st.k,[&](double x){
			return cfg.calc->assets(st.k, pw, x)-assets;
		});
	} else if (assets>0) {
		return numeric_search_r1(st.k,[&](double x){
			return cfg.calc->assets(st.k, pw, x)-assets;
		});
	} else {
		return st.k;
	}

}

PStrategy Strategy_Sinh_Gen::reset() const {
	return new Strategy_Sinh_Gen(cfg);
}

std::string_view Strategy_Sinh_Gen::getID() const {
	return id;
}

double Strategy_Sinh_Gen::calcInitialPosition(
		const IStockApi::MarketInfo &minfo, double price, double assets,
		double currency) const {
	return 0;
}

IStrategy::BudgetInfo Strategy_Sinh_Gen::getBudgetInfo() const {
	return {
		cfg.calc->budget(st.k, pw, st.p)+st.budget,
		cfg.calc->assets(st.k, pw, st.p)
	};
}

double Strategy_Sinh_Gen::calcCurrencyAllocation(double) const {
	return std::max(0.0, st.budget+cfg.calc->budget(st.k, pw, st.p)
			-(st.spot?cfg.calc->assets(st.k, pw, st.p)*st.p:0));
}

IStrategy::ChartPoint Strategy_Sinh_Gen::calcChart(double price) const {
	return {
		true,
		cfg.calc->assets(st.k, pw, price),
		cfg.calc->budget(st.k, pw, price)+st.budget,
	};
}

double Strategy_Sinh_Gen::getCenterPrice(double lastPrice,double assets) const {
	return lastPrice;
}

