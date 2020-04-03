/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_hyperbolic.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "sgn.h"

namespace {

const double accuracy = 1e-5;

}

using ondra_shared::logDebug;

std::string_view Strategy_Hyperbolic::id = "hyperbolic";

Strategy_Hyperbolic::Strategy_Hyperbolic(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}
Strategy_Hyperbolic::Strategy_Hyperbolic(const Config &cfg)
:cfg(cfg) {}


bool Strategy_Hyperbolic::isValid() const {
	return st.neutral_price > 0;
}



Strategy_Hyperbolic Strategy_Hyperbolic::init(const Config &cfg, double price, double pos, double currency) {
	double bal = (currency+ cfg.external_balance)/price;
	double mult = calcMult(pos+bal,cfg);
	double neutral = calcNeutral(mult, cfg.asym, pos, price);
	if (!std::isfinite(neutral) || neutral <= 0) neutral = price;
	return Strategy_Hyperbolic(cfg, State{neutral, price, pos, bal, calcPosValue(mult,cfg.asym,neutral,price)});
}


double Strategy_Hyperbolic::calcMult(double pos, const Config &cfg)  {
	return pos*cfg.power;
}

double Strategy_Hyperbolic::calcMult() const {
	return calcMult(st.position+st.bal+st.pos_offset, cfg);
}

double Strategy_Hyperbolic::calcPosition(double power, double asym, double neutral, double price) {
	return (neutral/price - 1 + asym) * power;
}

Strategy_Hyperbolic::PosCalcRes Strategy_Hyperbolic::calcPosition(double price) const {
	auto mm = calcRoots();
	bool lmt = false;
	if (price < mm.min) {price = mm.min; lmt = true;}
	if (price > mm.max) {price = mm.max; lmt = true;}
	if (lmt) {
		return {true,st.position};
	} else {
		/*double mult =calcMult();
		double pos = calcPosition(mult, cfg.asym, st.neutral_price, price);
		if (std::abs(pos) < std::abs(st.position)) {
			double base = mult * cfg.asym;
			double f = std::pow(0.5 * std::abs(pos - base)/mult,3) * cfg.reduction * 10;
			pos = calcPosition(mult, cfg.asym, st.neutral_price + (price - st.neutral_price)*f, price);
		}
		return {false, pos};
		*/



		double profit = st.position * (price - st.last_price);
		double new_neutral = cfg.reduction?calcNewNeutralFromProfit(profit, price):st.neutral_price;
		double pos = calcPosition(calcMult(), cfg.asym, new_neutral, price);
		return {false,pos};
	}
}

PStrategy Strategy_Hyperbolic::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double currency) const {
	if (isValid()) {
		if (st.bal == 0) {
			State nst = st;
			nst.bal = currency;
			return new Strategy_Hyperbolic(cfg, std::move(nst));
		} else {
			return this;
		}
	}
	else return new Strategy_Hyperbolic(init(cfg,ticker.last, assets, currency));
}

double Strategy_Hyperbolic::calcNewNeutralFromProfit(double profit, double price) const {
	if ((st.last_price - st.neutral_price) * (price - st.neutral_price) <= 0 || profit == 0)
			return st.neutral_price;

	double mult = calcMult();
	double middle = calcPrice0(st.neutral_price, cfg.asym);
	double prev_val = st.val;//calcPosValue(mult, cfg.asym, st.neutral_price, st.last_price);
	double cur_val = calcPosValue(mult, cfg.asym, st.neutral_price, price);
	double new_val;
	if (prev_val < 0 && (price - middle) * (st.neutral_price - middle)>0) {
		new_val = 2*cur_val - (prev_val - profit);
	} else {
		new_val = prev_val - profit;
	}
/*
	//calculate new value - we need use profit to move neutral price. so we need calculate desired new value
	double reduce_dir = prev_val < 0 && (price - middle) * (st.neutral_price - middle) >= 1?0:-1;

	double new_val = prev_val + reduce_dir*profit; //add profit to value (value reduces with profit, and increases with loss)
	double nv = cur_val - st.val;
	double pf = profit - nv;

	logDebug("Hyperbolic: pos = $6, new_val=$1, prev_val=$2, cur_val=$3, pf=$4, reduce_dir=$5, mult=$7", new_val, prev_val, cur_val, pf, reduce_dir, st.position, mult);
*/
	double new_neutral = st.neutral_price + (calcNeutralFromValue(mult, cfg.asym, st.neutral_price, new_val, price) - st.neutral_price)* 2 * cfg.reduction;

	double pos1 = calcPosition(mult, cfg.asym, st.neutral_price, price);
	double pos2 = calcPosition(mult, cfg.asym, new_neutral, price);
	if ((pos1 - st.position) * (pos2 - st.position) < 0) {
		return calcNeutral(mult, cfg.asym, st.position, price);
	} else {
		return new_neutral;

	}
/*

	//calculate price for new value (so on which price this value should happen)
	double new_price = calcPriceFromValue(mult, cfg.asym, st.neutral_price, new_val, price);

	logDebug("Hyperbolic: val_diff=$1", calcPosValue(mult,cfg.asym,st.neutral_price,new_price)-new_val);

	auto roots = calcRoots();
	if (roots.min < new_price && roots.max > new_price) {
		//calculate position for the new price
		double new_pos = calcPosition(mult, cfg.asym, st.neutral_price, new_price);
		double sanity_pos = calcPosition(mult, cfg.asym, st.neutral_price, price);
		if (std::isfinite(new_pos)) {
			//calculate new neutral, if we have new position
			double new_neutral = calcNeutral(mult, cfg.asym, new_pos, price);
			//combine with cfg.reduction to for new neutral
			return st.neutral_price + (new_neutral - st.neutral_price) * cfg.reduction * 2.0;
		}
	}
	return st.neutral_price; */
}

std::pair<Strategy_Hyperbolic::OnTradeResult, PStrategy> Strategy_Hyperbolic::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!isValid()) {
		return init(cfg,tradePrice, assetsLeft, currencyLeft)
				.onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);
	}

	State nwst = st;
	auto cpos = calcPosition(tradePrice);
	double mult = calcMult();
	double profit = (assetsLeft - tradeSize) * (tradePrice - st.last_price);
	nwst.neutral_price = calcNeutral(mult, cfg.asym, cpos.pos, tradePrice);
	double val = calcPosValue(mult, cfg.asym, nwst.neutral_price, tradePrice);
	//store last price
	nwst.last_price = tradePrice;
	//store current position
	nwst.position = cpos.pos;
	//calculate extra profit - we get change of value and add profit. This shows how effective is strategy. If extra is positive, it generates
	//profit, if it is negative, is losses
	double extra = (val - st.val) + profit;

	//store val to calculate next profit (because strategy was adjusted)
	nwst.val = val;
	//store new balance
	nwst.bal = st.bal + extra/tradePrice;

	nwst.pos_offset = calcPosition(nwst.bal,cfg.asym,st.neutral_price,st.neutral_price);

	return {
		OnTradeResult{extra,0,calcPrice0(st.neutral_price, cfg.asym),0},
		new Strategy_Hyperbolic(cfg,  std::move(nwst))
	};

}

json::Value Strategy_Hyperbolic::exportState() const {
	return json::Object
			("neutral_price",st.neutral_price)
			("last_price",st.last_price)
			("position",st.position)
			("bal",st.bal)
			("val",st.val)
			("ofs",st.pos_offset)
			("asym", static_cast<int>(cfg.asym * 1000)) ;

}

PStrategy Strategy_Hyperbolic::importState(json::Value src) const {
		State newst {
			src["neutral_price"].getNumber(),
			src["last_price"].getNumber(),
			src["position"].getNumber(),
			src["bal"].getNumber(),
			src["val"].getNumber(),
			src["ofs"].getNumber(),
		};
		if (src["asym"].getInt() != static_cast<int>(cfg.asym * 1000)) {
			newst.neutral_price = calcNeutral(calcMult(), cfg.asym, newst.position, newst.last_price);
		}
		return new Strategy_Hyperbolic(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_Hyperbolic::getNewOrder(
		const IStockApi::MarketInfo &,
		double curPrice, double price, double dir, double assets, double /*currency*/) const {
	auto mm = calcRoots();
	if (curPrice < mm.min || curPrice > mm.max) {
		if (dir * assets > 0) return {0,0,Alert::stoploss};
		else if (dir * assets < 0) return {0,-assets,Alert::stoploss};
		else return {0,0,Alert::forced};
	} else {
		auto cps = calcPosition(price);
		double diff = calcOrderSize(st.position, assets, cps.pos);
		return {0, diff};
	}
}

Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	return calcRoots();
}

double Strategy_Hyperbolic::getEquilibrium() const {
	return  st.last_price;
}

std::string_view Strategy_Hyperbolic::getID() const {
	return id;

}

PStrategy Strategy_Hyperbolic::reset() const {
	return new Strategy_Hyperbolic(cfg,{});
}

json::Value Strategy_Hyperbolic::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Position", (minfo.invert_price?-1:1)*st.position)
				  ("Last price", minfo.invert_price?1/st.last_price:st.last_price)
				 ("Neutral price", minfo.invert_price?1/st.neutral_price:st.neutral_price)
				 ("Value", st.val)
				 ("Multiplier", calcMult());


}

double Strategy_Hyperbolic::calcPosValue(double power, double asym, double neutral, double curPrice) {
	return power * ((asym - 1) * (neutral - curPrice) + neutral * (log(neutral) - log(curPrice)));
}

template<typename Fn>
static double numeric_search_r1(double middle, Fn &&fn) {
	double min = 0;
	double max = middle;
	double ref = fn(middle);
	if (ref == 0) return middle;
	double md = (min+max)/2;
	while (md > accuracy && (max - min) / md > accuracy) {
		double v = fn(md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return md;

}

template<typename Fn>
static double numeric_search_r2(double middle, Fn &&fn) {
	double min = 0;
	double max = 1.0/middle;
	double ref = fn(middle);
	if (ref == 0) return middle;
	double md = (min+max)/2;
	while (md * (1.0 / min - 1.0 / max) > accuracy) {
		double v = fn(1.0/md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return 1.0/md;

}


Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcRoots(double power,
		double asym, double neutral, double balance) {
	auto fncalc = [&](double x) {
		return calcPosValue(power,asym, neutral, x) - balance;
	};
	double m = calcPrice0(neutral, asym);
	double r1 = numeric_search_r1(m, fncalc);
	double r2 = numeric_search_r2(m, fncalc);
	//power * ((asym - 1) * (neutral - curPrice) + neutral * (log(neutral) - log(curPrice)));
/*	logDebug("Hyperbolic calc roots formula: $1*(($2-1)*($3-x)+$3*(ln($3)-ln(x)))-$4=0", power, asym, neutral, balance);
	logDebug("Hyperbolic calc roots: x1 = $1, x2 = $2 , V=[$3, $4]", r1,r2,m,fncalc(m));*/

	return {r1,r2};
}

double Strategy_Hyperbolic::calcMaxLoss() const {
	double lmt;
	if (cfg.max_loss == 0)
		lmt = st.bal;
	else
		lmt = cfg.max_loss;

	return lmt;
}

Strategy_Hyperbolic::MinMax Strategy_Hyperbolic::calcRoots() const {
	if (!rootsCache.has_value()) {
		double lmt = calcMaxLoss();
		rootsCache = calcRoots(calcMult(), cfg.asym,st.neutral_price,lmt);
	}
	return *rootsCache;
}

double Strategy_Hyperbolic::calcPrice0(double neutral_price, double asym) {
	double x = neutral_price/(1 -  asym);
	if (!std::isfinite(x)) return std::numeric_limits<double>::max();
	else return x;
}

double Strategy_Hyperbolic::calcNeutralFromPrice0(double price0, double asym) {
	return (1 - asym) * price0;
}

double Strategy_Hyperbolic::adjNeutral(double price, double value) const {
	double mult = calcMult();
	auto fncalc = [&](double x) {
		return calcPosValue(mult,cfg.asym, x, price) - value;
	};
	double m = calcPrice0(st.neutral_price, cfg.asym);
	double a = 0;
	double r;
	if (price < m) {
		r = numeric_search_r1(m, fncalc);
	} else if (price > m) {
		r = numeric_search_r2(m, fncalc);
	} else {
		r = st.neutral_price;
	}
	logDebug("Hyperbolic adjNeutral: old_n = $1, new_n = $2 ($3 %), cur_price = $4, middle_price=$5", st.neutral_price, r, a*100, price, m);
	return r;
}

double Strategy_Hyperbolic::calcValue0(double power, double asym, double neutral) {
	return neutral * power * (std::log(1 - asym) + asym);
}


double Strategy_Hyperbolic::calcNeutral(double power, double asym,
		double position, double curPrice) {
	return (curPrice * (position + power - power *  asym))/power;
}

double Strategy_Hyperbolic::calcPriceFromPosition(double power, double asym, double neutral, double position) {
	return (power * neutral)/(position + power - power *  asym);
}

double Strategy_Hyperbolic::calcPriceFromValue(double power, double asym,double neutral, double value, double curPrice) {
	auto fncalc = [&](double x) {
		return calcPosValue(power, asym, neutral, x) - value;
	};
	double m = calcPrice0(neutral, asym);
	double mv = calcValue0(power, asym, neutral);

	if (value <= mv) {
		return m;
	} else if (curPrice > m) {
		return numeric_search_r2(m, fncalc);
	} else if (curPrice < m) {
		return numeric_search_r1(m, fncalc);
	} else {
		return m;
	}
}

double Strategy_Hyperbolic::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
	auto m = calcPrice0(neutral, asym);
	auto fncalc = [&](double x) {
		double neutral = calcNeutralFromPrice0(x, asym);
		double v = calcPosValue(power, asym, neutral, curPrice);
		return v - value;
	};

	if (fncalc(curPrice) > 0)
		return neutral;

	double res;
	if (curPrice > m) {
		res = numeric_search_r1(curPrice, fncalc);
	} else if (curPrice < m) {
		res = numeric_search_r2(curPrice, fncalc);
	} else {
		res = m;
	}
	return calcNeutralFromPrice0(res, asym);
}

