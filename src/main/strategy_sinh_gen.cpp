/*
 * strategy_sinh_gen.cpp
 *
 *  Created on: 27. 5. 2021
 *      Author: ondra
 */


#include "strategy_sinh_gen.h"
#include <cmath>

#include <imtjson/string.h>
#include <imtjson/value.h>
#include <imtjson/binary.h>
#include "../shared/logOutput.h"
#include "numerical.h"
#include "sgn.h"

using ondra_shared::logInfo;
static const double INT_RANGE = 1000000;
static const double MAX_ERROR = 0.0001;


const std::string_view Strategy_Sinh_Gen::id = "sinh_gen";

bool Strategy_Sinh_Gen::FnCalc::sortPoints(const Point &a, const Point &b) {
	return a.first < b.first;
}

Strategy_Sinh_Gen::FnCalc::FnCalc(double wd, double boost,  double z)
:wd(wd*0.5),boost(boost),z(z) {

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
		logInfo("Strategy_Sinh_Gen: Integration table for: wd=$1, entries: $2", wd, itable.size());
}

double Strategy_Sinh_Gen::FnCalc::baseFn(double x) const {
	double y;
	double arg = wd*(1-x);
	y = std::sinh(arg);
	y = y / (std::pow(x,2*wd*z+1)*std::sqrt(wd));
	return y + sgn(y) * boost;
}


double Strategy_Sinh_Gen::FnCalc::root(double p) const {
	double guess = std::pow(10000,1.0/wd);
	double y = baseFn(guess);
	while (y>p) {
		guess = guess * guess;
		y = baseFn(guess);
		if (!std::isfinite(guess))
			return std::numeric_limits<double>::max();
	}
	return numeric_search_r1(guess, [&](double x){return baseFn(x)-p;});
}


double Strategy_Sinh_Gen::FnCalc::root(double k, double w, double x) const {
	double r = root(x/w);
	return r*k;
}
double Strategy_Sinh_Gen::FnCalc::root_of_k(double p, double w, double x) const {
    double r = root(x/w);
    return p/r;
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

double Strategy_Sinh_Gen::calcPower(const Config &cfg, const State &st) {
	return calcPower(cfg, st, st.k);
}
double Strategy_Sinh_Gen::calcPower(const Config &cfg, const State &st, double k) {
    //recalculate budget to price of neutral price (because budget is relative to current price)
    double adjbudget = calcPileBudget(cfg.ratio, calcPileKMult(st.p, st.budget, cfg.ratio), k);
	return cfg.power * adjbudget/k * st.pwadj;
}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg) :cfg(cfg),pw(cfg.power) {}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg, State &&st) :
		cfg(cfg), st(std::move(st)){
	pw = calcPower(cfg, st);
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

	double budget = st.budget>0?st.budget:(minfo.leverage?currency:(pos * price + currency));
	State nwst;
	nwst.p = st.p?st.p:price;
	nwst.spot = minfo.leverage == 0;
	nwst.avg_spread= st.avg_spread;
	nwst.pwadj = 1.0;
	double pw = cfg.power * currency/price;
	double prange = budget / price;
	double small_range = prange * 0.01;
	double initpos = calcPilePosition(cfg.ratio, calcPileKMult(price, budget, cfg.ratio), price);

	double rconst = calcPileKMult(price, budget, cfg.ratio);

	auto srchfn = [&](double x) {
        return cfg.calc->assets(x, pw, price)+initpos-pos;
    };

	for (int i=0;i < 5;i++) {
		if (pos > initpos + small_range) {
			nwst.k = numeric_search_r2(price, srchfn);
		} else if (pos < initpos - small_range) {
			nwst.k = numeric_search_r1(price, srchfn);
		} else {
			nwst.k = price;
		}
		nwst.budget = budget;
		pw = calcPower(cfg ,nwst);
	}
	nwst.offset = calcPilePosition(cfg.ratio, rconst, nwst.p);
	nwst.val = cfg.calc->budget(nwst.k, pw,  price);
	if (st.budget <= 0) {
		for (int i = 0; i < 10; i++) {
			nwst.budget = budget - nwst.val;
			pw = calcPower(cfg ,nwst);
			nwst.val = cfg.calc->budget(nwst.k, pw,  price);
		}
	}
	PStrategy s(new Strategy_Sinh_Gen(cfg, std::move(nwst)));
	if (!s->isValid()) throw std::runtime_error("Unable to initialize the strategy");
	return s;
}

double Strategy_Sinh_Gen::calcNewKFromValue(const Config &cfg, const State &st, double tradePrice, double pw,double enf_val) {
	double val = -std::abs(enf_val);
	double newk;
	if (enf_val<0) {
		newk = numeric_search_r2(tradePrice, [&](double k){
			return cfg.calc->budget(k, pw, tradePrice)-val;
		});
	} else {
		newk = numeric_search_r1(tradePrice, [&](double k){
			return cfg.calc->budget(k, pw, tradePrice)-val;
		});
	}
	if (newk<1e-60 || newk>1e60) return st.k;
	return newk;

}

double Strategy_Sinh_Gen::calcNewK(double tradePrice, double cb, double pnl, int bmode) const {
	if (st.rebalance) return st.k;
	if (cb > 0) {
		if (pnl >= 0) return st.k;
		else return tradePrice;
	}
	double newk = st.k;
	if (st.k != st.p) {
		if (!pnl) return st.k;
		double sprd = cfg.avgspread?(std::exp(st.avg_spread)):(tradePrice/st.p);
		double refp = st.k*sprd;
		double yield_a = cfg.calc->budget(st.k, pw, refp);
		double yield_b = cfg.calc->budget(st.k, pw, refp/pow2(sprd));
		double yield = std::min(yield_b,yield_a);
		double yield2 = yield+cfg.power*cfg.calc->getBoost()*std::abs(refp-st.k);
		double refb;
		switch (bmode) {
		default:
			case 0: refb = pnl>0?yield:0.0;break;
			case 1: refb = yield;break;
			case 2: refb = pnl>0?2*yield:0.0;break;
			case 3: refb = 0;break;
			case 4: refb = pnl>0?yield2*2:-yield2;break;
			case 5: refb = pnl>0?yield2*4:-2*yield2;break;
			case 6: refb = pnl>0?0.0:yield;break;
			case 7: refb = pnl>0?0.0:2*yield;break;
			case 8: refb = 2*yield;break;
			case 9: refb = pnl>0?-yield:yield;break;
			case 10: refb = pnl>0?-yield:2*yield;break;
			case 11: if (pnl>0) return st.k;refb = 0;break;
			case 12: if (pnl<0) return st.k;refb = 0;break;
            case 13: refb = pnl>0?0.0:3*yield;break;
            case 14: refb = pnl>0?-yield:3*yield;break;
            case 15: refb = cfg.calc->budget(st.k, pw, st.k*1.0001);break;
            case 16: refb = cfg.calc->budget(st.k, pw, st.k*1.0005);break;
            case 17: refb = cfg.calc->budget(st.k, pw, st.k*1.0010);break;
            case 18: refb = cfg.calc->budget(st.k, pw, st.k*1.0050);break;
            case 19: refb = cfg.calc->budget(st.k, pw, st.k*1.0100);break;
            case 20: refb = cfg.calc->budget(st.k, pw, st.k*1.0200);break;
            case 30: {
                    double np = cfg.calc->assets(st.k, pw, st.p);
                    double pval = std::abs(np*tradePrice);
                    if (pval>0) {
                        double lev = pval/st.budget;
                        refb = (pnl>0?2.0-lev:0.0)*yield;
                    } else {
                        return st.k;
                    }
                } break;
		}

		double nb = cb+pnl+refb; //current budget + pnl + yield = new budget

		if (nb > 0) {
/*		    if ((st.k - st.p) * (st.k - tradePrice) < 0) {
		        return st.k;
		    } else */{
			    return tradePrice;
		    }
		}

		if (st.p < st.k) {
			newk = numeric_search_r2(tradePrice, [&](double k){
				return cfg.calc->budget(k, calcPower(cfg ,st,k), tradePrice)-nb;
			});
			if (newk>1e300) newk = st.k;
		} else if (st.p > st.k) {
			newk = numeric_search_r1(tradePrice, [&](double k){
				return cfg.calc->budget(k, calcPower(cfg , st,k), tradePrice)-nb;
			});
			if (newk<1e-200) newk = st.k;
		}
	} else {
		newk = st.k;
	}

	if (!((newk <= tradePrice && newk >=st.k) || (newk>=tradePrice && newk <= st.k))) {
		newk = st.k;
	}
	return newk;

}

double Strategy_Sinh_Gen::roundZero(double assetsLeft,
		const IStockApi::MarketInfo &minfo, double tradePrice) const {
	double absass = std::abs(assetsLeft);
	if (absass < minfo.min_size || absass * tradePrice < minfo.min_volume
			|| absass < (st.budget / 1e8) / tradePrice) {
		assetsLeft = 0;
	}
	return assetsLeft;
}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_Sinh_Gen::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return init(minfo, tradePrice, assetsLeft, currencyLeft)
				->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	assetsLeft-=st.offset;

    double prevPos = roundZero(assetsLeft - tradeSize, minfo, st.p);
	assetsLeft = roundZero(assetsLeft, minfo, tradePrice);
	double cb = st.val;
	double pnl = prevPos*(tradePrice - st.p);
	double newk = calcNewK(tradePrice, cb, pnl, cfg.boostmode);
	double pwadj = adjustPower(prevPos, newk, tradePrice);
	double newpw = calcPower(cfg, st, newk);
	bool ulp = false;

	if (pwadj<1.0) {
		ulp = true;   //pokud se zmenil power, prepni na use_last_price
		pwadj = std::sqrt(pwadj); //a sniz zmenu poweru o mocninu... - aby nebyla tak drasticka redukce
	}

	double kmult = calcPileKMult(st.p, st.budget, cfg.ratio);
	double new_offset = calcPilePosition(cfg.ratio, kmult, tradePrice);


	double ppos = cfg.calc->assets(st.k, pw, st.p);
	double npos = cfg.calc->assets(newk, newpw*pwadj, tradePrice);
    double newbudget = calcPileBudget(cfg.ratio, kmult, tradePrice);
/*    newpw = newpw * newbudget / st.b*/
	npos = limitPosition(npos+new_offset) - new_offset;
	ppos = limitPosition(ppos+st.offset) - st.offset;
	if (tradeSize == 0 && st.p == st.k) newk = tradePrice;
	if (npos*ppos <= 0) {
		if (ppos || !npos) {
			newk = tradePrice;
		}
	}

	double nb = cfg.calc->budget(newk, newpw*pwadj, tradePrice);
	double np = pnl - (nb - cb);
	double posErr = std::abs(npos - assetsLeft)/(std::abs(assetsLeft)+std::abs(npos)); //< chyba pozice oproti vypoctu
	ulp = ulp || (tradeSize == 0 && posErr>0.3); //pokud je alert a chyba pozice je vetsi nez 30% prepni na use_last_price
	bool rbl = st.rebalance;
	if (posErr < 0.3) rbl = false; //rebalance se vypne, pokud je pozice s mensi chybou, nez je 30%


	double ofspnl = st.offset * (tradePrice - st.p);
	double ofsbchange = newbudget - st.budget;
    np = np + ofspnl - ofsbchange;


	State nwst;
	nwst.use_last_price = ulp;
	nwst.spot = minfo.leverage == 0;
	nwst.rebalance = rbl;
	nwst.budget = (cfg.reinvest?np:0)+newbudget;
	nwst.val = nb;
	nwst.k = newk;
	nwst.p = tradePrice;
	nwst.pwadj = st.pwadj*pwadj;
	nwst.offset = new_offset;

	double lspread = std::abs(std::log(tradePrice/st.p));
	nwst.avg_spread = nwst.avg_spread<=0?lspread*0.5:(299*st.avg_spread+lspread)/300;

//	if (st.rebalance) np = 0;
	return {
		OnTradeResult{np,0,newk,0},
		PStrategy(new Strategy_Sinh_Gen(cfg, std::move(nwst)))
	};
}


json::Value Strategy_Sinh_Gen::exportState() const {
	using namespace json;
	return Value(object,{
			Value("ulp", st.use_last_price),
			Value("rbl", st.rebalance),
			Value("k", st.k),
			Value("p", st.p),
			Value("budget", st.budget),
			Value("avg_spread", st.avg_spread),
			Value("val", st.val),
			Value("pwadj", st.pwadj),
			Value("hash", cfg.calcConfigHash())
	});
}

PStrategy Strategy_Sinh_Gen::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State nwst {
		minfo.leverage == 0,
		src["ulp"].getBool(),
		src["rbl"].getBool(),
		src["k"].getNumber(),
		src["p"].getNumber(),
		src["budget"].getNumber(),
		src["pwadj"].getValueOrDefault(1.0),
		0,
		src["avg_spread"].getNumber(),
		0,
	};
	nwst.offset = calcPilePosition(cfg.ratio,
	                               calcPileKMult(nwst.p, nwst.budget, cfg.ratio),
	                               nwst.p);

	if (src["hash"].hasValue() && cfg.calcConfigHash() != src["hash"].toString()) {
		nwst.k = 0; //make settings invalid;
	} else {
		json::Value val = src["val"];
		if (val.defined()) {
			nwst.val = val.getNumber();
		} else {
			nwst.val = cfg.calc->budget(nwst.k, calcPower(cfg,nwst), nwst.p);
		}
	}
	if (src["find_k"].defined()) {
		double find_k = src["find_k"].getNumber();
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg, nwst), find_k);
		nwst.val = cfg.calc->budget(nwst.k, calcPower(cfg,nwst), nwst.p);
		nwst.use_last_price = false;
		nwst.rebalance = true;
	}
	if (cfg.disableSide<0 && nwst.k < nwst.p) {
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg, nwst), -std::abs(nwst.val));
		nwst.use_last_price = false;
		nwst.rebalance = true;
	} else if (cfg.disableSide>0 && nwst.k > nwst.p) {
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg, nwst), +std::abs(nwst.val));
		nwst.use_last_price = false;
		nwst.rebalance = true;
	}

	return new Strategy_Sinh_Gen(cfg, std::move(nwst));
}

json::Value Strategy_Sinh_Gen::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	auto getpx = [&](double px){return minfo.invert_price?1.0/px:px;};
	auto getpos = [&](double pos){return minfo.invert_price?-pos:pos;};
	double sprd = std::exp(st.avg_spread);
	double as = cfg.calc->assets(st.k, pw, st.p);

	using namespace json;

	return Value(object, {
			Value("Leverage[x]", std::abs(as)*st.p/(st.val+st.budget)),
			Value("Power[%]", st.pwadj*100),
			Value("Price-last", getpx(st.p)),
			Value("Price-neutral", getpx(st.k)),
			Value("Budget-total", st.budget),
			Value("Budget-current", st.val+st.budget),
			Value("Position", getpos(as+st.offset)),
			Value("Position offset", getpos(st.offset)),
			Value("Use last price",st.use_last_price),
			Value("Profit per trade", -cfg.calc->budget(st.k, pw, st.k*sprd)),
			Value("Profit per trade[%]", -cfg.calc->budget(st.k, pw, st.k*sprd)/st.budget*100)
	});
}

IStrategy::OrderData Strategy_Sinh_Gen::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double curpos = assets -  st.offset;

	curpos = roundZero(curpos, minfo, st.p);

	if (st.rebalance) new_price = cur_price;


	if (limitPosition(curpos+st.offset)-st.offset != curpos && dir * curpos < 0) {
		return {cur_price, -curpos, Alert::forced};
	}


	double calc_price = new_price;
	if (((cfg.lazyopen && dir * curpos >= 0) || (cfg.lazyclose && dir * curpos < 0)) && !st.rebalance && st.avg_spread>0) {
		//calc_price - use average spread instead current price
		calc_price = getEquilibrium_inner(curpos) * std::exp(-2*dir*st.avg_spread);
		if (calc_price*dir < new_price*dir) {
			//however can't go beyond new_price
			calc_price = new_price;
		} else {
			double a = cfg.calc->assets(st.k, pw, calc_price);
			if (roundZero(a-curpos, minfo, calc_price) == 0) {
				calc_price = new_price;
			}
		}
	}

	//calculate pnl
	double pnl = curpos*(calc_price - st.p);
	//calculate new k for budgetr and pnl
	double newk = calcNewK(calc_price, st.val, pnl, cfg.boostmode);

    double npw = calcPower(cfg, st, newk);;

	//calculate minimal allowed budget
	//	double minbudget = st.budget*(1.0-cfg.stopOnLoss);
	double pwadj = adjustPower(curpos, newk, calc_price);

	double kmult = calcPileKMult(st.p, st.budget, cfg.ratio);
    double new_offset = calcPilePosition(cfg.ratio, kmult, new_price);
/*    double pile_budget = calcPileBudget(cfg.ratio, kmult, new_price);
    double budget_ratio = pile_budget/st.budget;*/

    double new_pos = cfg.calc->assets(newk, npw*pwadj/*budget_ratio*/, calc_price);
    if (new_pos * curpos < 0) new_pos = 0; //force close position, if goes to other side
    double finpos = new_pos + new_offset;
    double f  = limitPosition(finpos);
    bool limited = f != finpos;


	double dfa = f - assets;

	return {new_price, dfa, limited?Alert::forced:Alert::enabled};
}

double Strategy_Sinh_Gen::limitPosition(double pos) const {
	if ((cfg.disableSide<0 || st.spot) && pos<0) {
		return 0;
	}
	if (cfg.disableSide>0 && pos>0) {
		return 0;
	}
	return pos;
}

IStrategy::MinMax Strategy_Sinh_Gen::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {

    if (minfo.leverage) {
        double b = currencies;
        MinMax ret;
        ret.min = numeric_search_r1(st.k, [&](double x){
            return cfg.calc->budget(st.k, pw, x)+b;
        });
        if (ret.min < 1e-100) ret.min = 0;
        ret.max = numeric_search_r2(st.k, [&](double x){
            return cfg.calc->budget(st.k, pw, x)+b;
        });
        if (ret.max > 1e100) ret.max = std::numeric_limits<double>::infinity();
        return ret;
    } else {
        MinMax ret;
        ret.max = numeric_search_r2(st.k, [&](double x){
           return cfg.calc->assets(st.k, pw, x)  + st.offset;
        });
        ret.min = numeric_search_r1(st.k, [&](double x){
           return cfg.calc->budget(st.k, pw, x) - 
                   x*(cfg.calc->assets(st.k, pw, x) + st.offset) + st.budget;
        });
        return ret;
    }
}

double Strategy_Sinh_Gen::getEquilibrium(double assets) const {
	return getEquilibrium_inner(assets-st.offset);
}
double Strategy_Sinh_Gen::getEquilibrium_inner(double assets) const {
	double r = cfg.calc->root(st.k, pw, assets);
	return r;
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
    double budget = currency + (minfo.leverage?0:(assets * price));
    double kmult = calcPileKMult(price, budget, cfg.ratio);
    double pos = calcPilePosition(cfg.ratio, kmult, price);
    return pos;

}

IStrategy::BudgetInfo Strategy_Sinh_Gen::getBudgetInfo() const {
	return {st.budget,0};
}

double Strategy_Sinh_Gen::calcCurrencyAllocation(double p, bool leveraged) const {
    double pos = cfg.calc->assets(st.k, pw, st.p)+st.offset;
    double posval = pos * st.p;
    double posvalchg = pos * (p - st.p);
    if (leveraged) {
        return st.budget + st.val + posvalchg;
    } else {
        return st.budget + st.val - posval;
    }
/*	if (st.spot) {
		return std::max(0.0, st.budget+cfg.calc->budget(st.k, pw, st.p)
				-cfg.calc->assets(st.k, pw, st.p)*st.p);
	} else {
		double assets = cfg.calc->assets(st.k, pw, st.p);
		double pnl = (p - st.p) * assets;
		double newk = calcNewK(p, st.val, pnl, cfg.boostmode);
		return std::max(0.0, st.budget+cfg.calc->budget(newk, calcPower(cfg, st, newk), p));
	}*/
}


IStrategy::ChartPoint Strategy_Sinh_Gen::calcChart(double price) const {
	return {
		true,
		cfg.calc->assets(st.k, pw, price)+st.offset,
		cfg.calc->budget(st.k, pw, price)+st.budget+st.offset * (price - st.p),
	};
}

double Strategy_Sinh_Gen::getCenterPrice(double lastPrice,double assets) const {
	if (st.use_last_price) return lastPrice;
	else return getEquilibrium(assets);
}

json::String Strategy_Sinh_Gen::Config::calcConfigHash() const {
	std::hash<json::Value> h;
	return json::Value(h({power, calc->getWD()})).toString();
}

double Strategy_Sinh_Gen::adjustPower(double a, double newk, double price) const {
	if (cfg.openlimit && !st.rebalance) {
		double new_a = cfg.calc->assets(newk, pw, price);
		double b = st.budget+cfg.calc->budget(newk, pw, price);
		double newlev = std::abs(new_a) * price/b;
		double oldlev = std::abs(a) * price/b;
		if (newlev > 0.25 || cfg.openlimit<0) {
			if (cfg.openlimit>0) {
				double m = new_a/a;
				double f = cfg.openlimit+1;
				if (m > f) {
					new_a = a * f;
					double adj = numeric_search_r1(1.5, [&](double adj){
						return cfg.calc->assets(newk, pw*adj, price) - new_a;
					});
					return std::max(0.3,adj);
				}
			} else {
				double ref_a = (oldlev-cfg.openlimit)*b/price;
				if (ref_a < std::abs(new_a)) {
					new_a = sgn(new_a)*ref_a;
//					new_a = pow2(ref_a)/new_a;
					double adj = numeric_search_r1(1.5, [&](double adj){
						return cfg.calc->assets(newk, pw*adj, price) - new_a;
					});
					return std::max(0.1,adj);
				} else if (st.pwadj<1.0 && (price - st.p)*a < 0) {
					return (st.pwadj*0.98+0.02)/st.pwadj;
				}
			}
		} else if (st.pwadj<1.0 && (price - st.p)*a < 0) {
			return (st.pwadj*0.98+0.02)/st.pwadj;
		}
	}else {
		return 1.0/st.pwadj;
	}
	return 1.0;
}


double Strategy_Sinh_Gen::calcPileKMult(double price, double budget, double ratio) {
    double c = calcPileBudget(ratio, 1, price);
    double kmult = budget/c;
    return kmult;
}

double Strategy_Sinh_Gen::calcPilePosition(double ratio, double kmult, double price) {
    return kmult*ratio*std::pow(price, ratio-1);
}

double Strategy_Sinh_Gen::calcPileBudget(double ratio, double kmult, double price) {
    return kmult*std::pow(price,ratio);
}

double Strategy_Sinh_Gen::calcPileEquilibrium(double ratio, double kmul, double position) {
    //(c/(k z))^(1/(-1 + z))
    return std::pow<double>(position/(kmul*ratio),1.0/(ratio-1));
}

double Strategy_Sinh_Gen::calcPilePriceFromBudget(double ratio, double kmul, double budget) {
    //(c/k)^(1/z)
    return std::pow(budget/kmul, 1.0/(ratio));
}

double Strategy_Sinh_Gen::calcPileCurrency(double ratio, double kmult, double price) {
    return kmult*std::pow(price,ratio)*(1 - ratio);
}

double Strategy_Sinh_Gen::calcPilePriceFromCurrency(double ratio, double kmult, double currency) {
    //((k - k z)/c)^(-1/z)
    return std::pow((kmult - kmult*ratio)/currency,-1.0/ratio);
}
