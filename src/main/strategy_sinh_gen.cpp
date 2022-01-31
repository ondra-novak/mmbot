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
:wd(wd),boost(boost),z(z) {

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
	double arg = wd*(1-std::sqrt(x));
	y = std::sinh(arg);
	y = y / (std::pow(x,wd*z+1)*std::sqrt(wd));
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

double Strategy_Sinh_Gen::calcPower(double cfgpw, const State &st) {
	return calcPower(cfgpw, st, st.k);
}
double Strategy_Sinh_Gen::calcPower(double cfgpw, const State &st, double k) {
	return cfgpw * st.budget/k * st.pwadj;
}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg) :cfg(cfg),pw(cfg.power) {}

Strategy_Sinh_Gen::Strategy_Sinh_Gen(const Config &cfg, State &&st) :
		cfg(cfg), st(std::move(st)){
	pw = calcPower(cfg.power, st);
}

bool Strategy_Sinh_Gen::isValid() const {
	return st.k>0 && st.p>0 && st.budget>0;
}

PStrategy Strategy_Sinh_Gen::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(minfo, curTicker.last, assets-cfg.offset, currency);
	else return this;
}

PStrategy Strategy_Sinh_Gen::init(const IStockApi::MarketInfo &minfo,
		double price, double pos, double currency) const {

	double budget = st.budget>0?st.budget:(minfo.leverage?currency:(pos * price + currency));
	State nwst;
	nwst.p = st.p?st.p:price;
	nwst.spot = minfo.leverage == 0;
	nwst.sum_spread = st.sum_spread;
	nwst.trades = st.trades;
	nwst.last_spread = st.last_spread;
	nwst.pwadj = 1.0;
	double pw = cfg.power * currency/price;
	double zero =  cfg.calc->assets(price, pw, price);;

	for (int i=0;i < 5;i++) {
		if (pos > 0) {
			if (zero>=pos || pos * zero < 0) {
				nwst.k = price;
			} else {
				nwst.k = numeric_search_r2(price, [&](double x) {
					return cfg.calc->assets(x, pw, price)-pos;
				});
			}
		} else if (pos < 0) {
			if (zero<=pos || pos * zero < 0) {
				nwst.k = price;
			} else {
				nwst.k = numeric_search_r1(price, [&](double x) {
					return cfg.calc->assets(x, pw, price)-pos;
				});
			}
		} else {
			nwst.k = price;
		}
		nwst.budget = budget;
		pw = calcPower(cfg.power ,nwst);
	}
	nwst.val = cfg.calc->budget(nwst.k, pw,  price);
	if (st.budget <= 0) {
		for (int i = 0; i < 10; i++) {
			nwst.budget = budget - nwst.val;
			pw = calcPower(cfg.power ,nwst);
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
		double sprd = cfg.avgspread?(1.0+st.sum_spread/std::max(st.trades,10)):(tradePrice/st.p);
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
		}

		double nb = cb+pnl+refb; //current budget + pnl + yield = new budget

		if (nb > 0) {
			if (pnl>=0) return st.k;
			else return tradePrice;
		}

		if (st.p < st.k) {
			newk = numeric_search_r2(tradePrice, [&](double k){
				return cfg.calc->budget(k, calcPower(cfg.power ,st,k), tradePrice)-nb;
			});
			if (newk>1e300) newk = st.k;
		} else if (st.p > st.k) {
			newk = numeric_search_r1(tradePrice, [&](double k){
				return cfg.calc->budget(k, calcPower(cfg.power , st,k), tradePrice)-nb;
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

	assetsLeft-=cfg.offset;

	assetsLeft = roundZero(assetsLeft, minfo, tradePrice);
	double prevPos = assetsLeft - tradeSize;
	double cb = st.val;
	double pnl = prevPos*(tradePrice - st.p);
	double newk = calcNewK(tradePrice, cb, pnl, cfg.boostmode);
	double pwadj = adjustPower(prevPos, newk, tradePrice);
	double newpw = calcPower(cfg.power, st, newk);
	bool ulp = false;

	if (pwadj<1.0) {
		ulp = true;   //pokud se zmenil power, prepni na use_last_price
		pwadj = std::sqrt(pwadj); //a sniz zmenu poweru o mocninu... - aby nebyla tak drasticka redukce
	}

	double nb = cfg.calc->budget(newk, newpw*pwadj, tradePrice);
	double np = pnl - (nb - cb);
	double npos = cfg.calc->assets(newk, newpw*pwadj, tradePrice);
	npos = limitPosition(npos);
	double ppos = cfg.calc->assets(st.k, pw, st.p);
	ppos = limitPosition(ppos);
	double posErr = std::abs(npos - assetsLeft)/(std::abs(assetsLeft)+std::abs(npos)); //< chyba pozice oproti vypoctu
	ulp = ulp || (tradeSize == 0 && posErr>0.3); //pokud je alert a chyba pozice je vetsi nez 30% prepni na use_last_price
	bool rbl = st.rebalance;
	if (posErr < 0.3) rbl = false; //rebalance se vypne, pokud je pozice s mensi chybou, nez je 30%


	if (tradeSize == 0 && st.p == st.k) newk = tradePrice;
	if (npos*ppos <= 0) {
		if (ppos || !npos) {
			newk = tradePrice;
		}
	}


	State nwst;
	nwst.use_last_price = ulp;
	nwst.spot = minfo.leverage == 0;
	nwst.rebalance = rbl;
	nwst.budget = (cfg.reinvest?np:0)+st.budget;
	nwst.val = nb;
	nwst.k = newk;
	nwst.p = tradePrice;
	nwst.pwadj = st.pwadj*pwadj;
	nwst.last_spread = tradePrice/st.p;
	if (nwst.last_spread<1.0) nwst.last_spread=1.0/nwst.last_spread;
	if (nwst.last_spread>1.0) {
		nwst.trades = st.trades+1;
		nwst.sum_spread = st.sum_spread + (nwst.last_spread-1.0);
	}

	if (st.rebalance) np = 0;
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
			Value("last_spread", st.last_spread),
			Value("sum_spread", st.sum_spread),
			Value("trades", st.trades),
			Value("val", st.val),
			Value("pwadj", st.pwadj),
			Value("hash", cfg.calcConfigHash()),
			Value("offset", json::base64url->encodeBinaryValue(json::BinaryView(
					reinterpret_cast<const unsigned char *>(&cfg.offset), sizeof(cfg.offset))))
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
		src["last_spread"].getNumber(),
		src["sum_spread"].getNumber(),
		static_cast<int>(src["trades"].getInt()),
	};
	if (src["hash"].hasValue() && cfg.calcConfigHash() != src["hash"].toString()) {
		nwst.k = 0; //make settings invalid;
	} else {
		json::Value val = src["val"];
		if (val.defined()) {
			nwst.val = val.getNumber();
		} else {
			nwst.val = cfg.calc->budget(nwst.k, calcPower(cfg.power,nwst), nwst.p);
		}
	}
	if (src["find_k"].defined()) {
		double find_k = src["find_k"].getNumber();
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg.power, nwst), find_k);
		nwst.val = cfg.calc->budget(nwst.k, calcPower(cfg.power,nwst), nwst.p);
		nwst.use_last_price = false;
		nwst.rebalance = true;
	}
	if (cfg.disableSide<0 && nwst.k < nwst.p) {
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg.power, nwst), -std::abs(nwst.val));
		nwst.use_last_price = false;
		nwst.rebalance = true;
	} else if (cfg.disableSide>0 && nwst.k > nwst.p) {
		nwst.k = calcNewKFromValue(cfg,nwst, nwst.p, calcPower(cfg.power, nwst), +std::abs(nwst.val));
		nwst.use_last_price = false;
		nwst.rebalance = true;
	}
	json::Value binofs=json::base64url->encodeBinaryValue(json::BinaryView(
						reinterpret_cast<const unsigned char *>(&cfg.offset), sizeof(cfg.offset)));
	if(src["offset"].hasValue() && binofs != src["offset"]) {
		nwst.rebalance = true;
	}

	return new Strategy_Sinh_Gen(cfg, std::move(nwst));
}

json::Value Strategy_Sinh_Gen::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	auto getpx = [&](double px){return minfo.invert_price?1.0/px:px;};
	auto getpos = [&](double pos){return minfo.invert_price?-pos:pos;};
	double sprd = cfg.avgspread?(1.0+st.sum_spread/std::max(10,st.trades)):st.last_spread;
	double a = cfg.calc->assets(st.k, pw, st.p);

	using namespace json;

	return Value(object, {
			Value("Leverage[x]", std::abs(a)*st.p/(st.val+st.budget)),
			Value("Power[%]", st.pwadj*100),
			Value("Price-last", getpx(st.p)),
			Value("Price-neutral", getpx(st.k)),
			Value("Budget-total", st.budget),
			Value("Budget-current", st.val+st.budget),
			Value("Position", getpos(a+cfg.offset)),
			Value("Use last price",st.use_last_price),
			Value("Profit per trade", -cfg.calc->budget(st.k, pw, st.k*sprd)),
			Value("Profit per trade[%]", -cfg.calc->budget(st.k, pw, st.k*sprd)/st.budget*100)
	});
}

IStrategy::OrderData Strategy_Sinh_Gen::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	assets -= cfg.offset;

	assets = roundZero(assets, minfo, st.p);

	if (st.rebalance) new_price = cur_price;


	if (limitPosition(assets) != assets && dir * assets < 0) {
		return {cur_price, -assets, Alert::forced};
	}

	double calc_price = new_price;
	if (((cfg.lazyopen && dir * assets >= 0) || (cfg.lazyclose && dir * assets < 0)) && !st.rebalance && st.trades>1) {
		//calc_price - use average spread instead current price
		calc_price = getEquilibrium_inner(assets) * std::exp(-1.5*dir*st.sum_spread/st.trades);
		if (calc_price*dir < new_price*dir) {
			//however can't go beyond new_price
			calc_price = new_price;
		} else {
			double a = cfg.calc->assets(st.k, pw, calc_price);
			if (roundZero(a-assets, minfo, calc_price) == 0) {
				calc_price = new_price;
			}
		}
	}

	//calculate pnl
	double pnl = assets*(calc_price - st.p);
	//calculate new k for budgetr and pnl
	double newk = calcNewK(calc_price, st.val, pnl, cfg.boostmode);
	//calculate minimal allowed budget
	//	double minbudget = st.budget*(1.0-cfg.stopOnLoss);
	double pwadj = adjustPower(assets, newk, calc_price);


	double new_pos = limitPosition(cfg.calc->assets(newk, pw*pwadj, calc_price));
	if (cfg.disableSide) new_pos = roundZero(new_pos-cfg.offset, minfo, new_price)+cfg.offset;
	double dfa = new_pos -assets;
	if ((new_pos * assets <0 || new_pos == 0) && (assets * dir <= 0)){
		//close current position (force alert)
		return {calc_price,-assets,Alert::forced};
	}
	return {new_price, dfa};
}

double Strategy_Sinh_Gen::limitPosition(double pos) const {
	double apos = pos + cfg.offset;
	if ((cfg.disableSide<0 || st.spot) && apos<0) {
		return -cfg.offset;
	}
	if (cfg.disableSide>0 && apos>0) {
		return -cfg.offset;
	}
	return pos;
}

IStrategy::MinMax Strategy_Sinh_Gen::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double ,
		double currencies) const {

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
}

double Strategy_Sinh_Gen::getEquilibrium(double assets) const {
	return getEquilibrium_inner(assets-cfg.offset);
}
double Strategy_Sinh_Gen::getEquilibrium_inner(double assets) const {
	if (std::abs(assets) < std::abs(cfg.calc->assets(st.k, pw, st.k)))
		return st.p;
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
	auto me = init(minfo,price,assets,currency);
	const Strategy_Sinh_Gen *mm = static_cast<const Strategy_Sinh_Gen *>((const IStrategy *)me);
	double k = mm->st.k;
	double pw = mm->pw;
	return cfg.offset+cfg.calc->assets(k,pw,k);
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
