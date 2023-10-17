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
		a = std::max(a,0.1);
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
			nwst.at_zero = false;
		} else if (pos < initpos - small_range) {
			nwst.k = numeric_search_r1(price, srchfn);
			nwst.at_zero = false;
		} else {
			nwst.k = price;
			nwst.at_zero = true;
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

    if ((tradePrice - st.k) * (st.p - st.k) < 0) {
        return tradePrice;
    }

    double newk = st.k;

    if (bmode == 35 || bmode == 36) {
        double nb = st.val;
        nb += pnl;
        double spread = 1.0 - std::min(st.p, tradePrice) / std::max(st.p, tradePrice);
        if (bmode == 35) spread = 0.01;
        double profit = -st.budget * cfg.custom_spread * spread;
        if (pnl < 0) profit = 0;
        else if (st.k == st.p) profit*=0.5;
        nb += profit;
        if (nb < 0) {
            if (tradePrice > st.k) {
                newk = numeric_search_r1(tradePrice, [&](double k){
                    return cfg.calc->budget(k, calcPower(cfg , st,k), tradePrice)-nb;
                });
                if (newk<1e-200) newk = st.k; //failed to search
            }
            else if (tradePrice < st.k) {
                newk = numeric_search_r2(tradePrice, [&](double k){
                    return cfg.calc->budget(k, calcPower(cfg ,st,k), tradePrice)-nb;
                });
                if (newk>1e300) newk = st.k; //failed to search
            }
        }
        if (st.k == st.p) {
            double newk2 = std::sqrt(st.k*tradePrice);
            if (tradePrice < st.k) newk = std::max(newk, newk2);
            else newk = std::min(newk,newk2);
        }
        return newk;

    }



	if (st.at_zero) return std::sqrt(st.k*tradePrice);
//	if (st.at_zero) return st.k;



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
        case 15: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0002):0.0;break;
        case 16: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0010):0.0;break;
        case 17: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0020):0.0;break;
        case 18: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0100):0.0;break;
        case 19: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0200):0.0;break;
        case 20: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*1.0400):0.0;break;
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

        case 32: refb = pnl>0?cfg.calc->budget(st.k, pw, st.k*(1+cfg.custom_spread)):0.0;break;
        case 33:
            if (pnl > 0) {
                double sp = -cfg.calc->budget(st.k, pw, st.k*(1+cfg.custom_spread));
                refb = sp - pnl;
            } else {
                refb = 0;
            }
            break;
        case 34:
            if (pnl > 0) {
                return st.k;
            } else {
                double sp = cfg.calc->budget(st.k, pw, st.k*(1+cfg.custom_spread));
                refb = -sp;
            }
            break;
        case 35:
            return st.k;
            break;

    }

    double nb = cb+pnl+refb; //current budget + pnl + yield = new budget

    if (nb > 0) {       //budget can't be positive, in this case, we close position at tradePrice
        return tradePrice;
    }

    if (st.p < st.k) {
        newk = numeric_search_r2(tradePrice, [&](double k){
            return cfg.calc->budget(k, calcPower(cfg ,st,k), tradePrice)-nb;
        });
        if (newk>1e300) newk = st.k; //failed to search
    } else if (st.p > st.k) {
        newk = numeric_search_r1(tradePrice, [&](double k){
            return cfg.calc->budget(k, calcPower(cfg , st,k), tradePrice)-nb;
        });
        if (newk<1e-200) newk = st.k; //failed to search
    }

    ///distance newk from tradePrice must be less then previous distance
    if (std::abs(newk - tradePrice) > std::abs(st.k - tradePrice)) {
        //otherwise, don't change k
        newk = st.k;
    }
	return newk;

}

double Strategy_Sinh_Gen::roundZero(double assetsLeft,
		const IStockApi::MarketInfo &minfo, double tradePrice) const {
	double absass = std::abs(assetsLeft);
	if (absass < minfo.calcMinSize(tradePrice)) {
		assetsLeft = 0;
	}
	return assetsLeft;
}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_Sinh_Gen::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return init(minfo, tradePrice, assetsLeft, currencyLeft)
				->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

    if (tradeSize == 0 && !st.at_zero && st.p2 && (assetsLeft * (st.p2 - tradePrice) > 0)) {
        State nwst = st;
        nwst.p2 = tradePrice;
        nwst.use_last_price = true;
        return {
            {0,0,nwst.k,0},
            new Strategy_Sinh_Gen(cfg, std::move(nwst))
        };
    }

	NewPosInfo npinfo = calcNewPos(minfo, tradePrice, assetsLeft - tradeSize, tradeSize == 0);

    if (tradeSize == 0 && !st.at_zero && roundZero(assetsLeft-st.offset, minfo, tradePrice) == 0) {
        npinfo.is_close = true;
        npinfo.newk = tradePrice;
        npinfo.newpos = 0;
    }
    if (tradeSize == 0 && st.at_zero) {
        double dff = tradePrice - st.p;
        if (cfg.disableSide * dff >= 0) return {{0,0,st.k,0}, this};

    }

	double newbudget = calcPileBudget(cfg.ratio, npinfo.pilekmul, tradePrice);

	State nwst;
	nwst.at_zero = npinfo.is_close;
	nwst.budget = newbudget;
	nwst.k = npinfo.newk;
	nwst.offset = npinfo.newofs;
	nwst.p = tradePrice;
	nwst.p2 = tradePrice;
	nwst.pwadj = npinfo.newpwadj;
	nwst.rebalance = st.rebalance;
	nwst.use_last_price = false;
	nwst.spot = st.spot;
	nwst.val = cfg.calc->budget(npinfo.newk, npinfo.newpw*npinfo.newpwadj, tradePrice);
	double lspread = std::abs(std::log(tradePrice/st.p));
	nwst.avg_spread = st.avg_spread<=0?(lspread*0.5):((299*st.avg_spread+lspread)/300);

    if (std::abs(assetsLeft - st.offset) < minfo.calcMinSize(tradePrice)) {
        nwst.at_zero = true;
        nwst.k = tradePrice;
    }



	double vnp = npinfo.pnl - nwst.val + st.val;

	double ofspnl = st.offset * (tradePrice - st.p);
    double ofsnp =  ofspnl - newbudget + st.budget;;

    if (cfg.reinvest) nwst.budget+=vnp;

    double posErr = std::abs(npinfo.newpos - assetsLeft)/(std::abs(assetsLeft)+std::abs(npinfo.newpos)); //< chyba pozice oproti vypoctu
    nwst.use_last_price = nwst.use_last_price || (tradeSize == 0 && posErr>0.3); //pokud je alert a chyba pozice je vetsi nez 30% prepni na use_last_price
    if (posErr < 0.3) nwst.rebalance = false; //rebalance se vypne, pokud je pozice s mensi chybou, nez je 30%

    return {
        OnTradeResult{vnp+ofsnp,0,npinfo.newk,0},
        PStrategy(new Strategy_Sinh_Gen(cfg, std::move(nwst)))
    };



}


json::Value Strategy_Sinh_Gen::exportState() const {
	using namespace json;
	return Value(object,{
			Value("ulp", st.use_last_price),
			Value("rbl", st.rebalance),
			Value("atz", st.at_zero),
			Value("k", st.k),
			Value("p", st.p),
			Value("p2", st.p2),
			Value("budget", st.budget),
			Value("avg_spread", st.avg_spread),
			Value("val", st.val),
			Value("pwadj", st.pwadj)
	});
}

PStrategy Strategy_Sinh_Gen::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State nwst {
		minfo.leverage == 0,
		src["ulp"].getBool(),
		src["rbl"].getBool(),
		src["atz"].getBool(),
		src["k"].getNumber(),
		src["p"].getNumber(),
        src["p2"].getNumber(),
		src["budget"].getNumber(),
		src["pwadj"].getValueOrDefault(1.0),
		0,
		src["avg_spread"].getNumber(),
		0,
	};
	nwst.offset = calcPilePosition(cfg.ratio,
	                               calcPileKMult(nwst.p, nwst.budget, cfg.ratio),
	                               nwst.p);

    json::Value val = src["val"];
    if (val.defined()) {
        nwst.val = val.getNumber();
    } else {
        nwst.val = cfg.calc->budget(nwst.k, calcPower(cfg,nwst), nwst.p);
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
//	double sprd = std::exp(st.avg_spread);
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
			Value("Spread-average[%]", st.avg_spread*100),
			Value("At zero", st.at_zero)
	});
}

Strategy_Sinh_Gen::NewPosInfo Strategy_Sinh_Gen::calcNewPos(const IStockApi::MarketInfo &minfo, double new_price, double assets, bool alert) const {

    double curpos = assets - st.offset;

    curpos = roundZero(curpos, minfo, st.p);
    double cur_calc_pos = cfg.calc->assets(st.k, pw, st.p);

    double pnl = curpos*(new_price - st.p);
    double newk = calcNewK(new_price, st.val, pnl, alert?3:cfg.boostmode);
    double npw = calcPower(cfg, st, newk);
    double pwadj = adjustPower(curpos, newk, new_price);
    double new_pos = cfg.calc->assets(newk, npw*pwadj, new_price);
    double kmult = calcPileKMult(st.p, st.budget, cfg.ratio);
    double new_offset = calcPilePosition(cfg.ratio, kmult, new_price);
    bool is_close = false;
    //if we not at zero and position goes to otherside
    if (new_pos * cur_calc_pos < 0) {
        if (!st.at_zero && curpos) {
            new_pos = 0;
            is_close = true;
            newk = new_price;
        } else {
            newk = (new_price + st.p)*0.5;
            npw = calcPower(cfg, st, newk);
            pwadj = 1.0;
            new_pos = cfg.calc->assets(newk, npw*pwadj, new_price);
        }
    }
    if (roundZero(new_pos, minfo, new_price) == 0) {
        is_close = true;
    }
    return {
        pnl,
        newk,
        new_pos,
        npw,
        pwadj,
        new_offset,
        kmult,
        is_close
    };



}

IStrategy::OrderData Strategy_Sinh_Gen::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {


    NewPosInfo nposinfo = calcNewPos(minfo, new_price, assets,false);

    double l = nposinfo.newpos;


    double f = l+nposinfo.newofs;
    double lf = limitPosition(f);

    double df = lf - assets;

    return {new_price, df, f != lf?Alert::forced:Alert::enabled};
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
