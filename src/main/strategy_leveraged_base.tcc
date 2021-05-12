/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_leveraged_base.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "../imtjson/src/imtjson/string.h"
#include "sgn.h"


using ondra_shared::logDebug;

template<typename Calc>
std::string_view Strategy_Leveraged<Calc>::id = Calc::id;

template<typename Calc>
Strategy_Leveraged<Calc>::Strategy_Leveraged(const PCalc &calc, const PConfig &cfg, State &&st)
:calc(calc),cfg(cfg), st(std::move(st)) {}
template<typename Calc>
Strategy_Leveraged<Calc>::Strategy_Leveraged(const PCalc &calc, const PConfig &cfg)
:calc(calc),cfg(cfg) {}


template<typename Calc>
bool Strategy_Leveraged<Calc>::isValid() const {
	return st.neutral_price > 0 && st.power > 0 && st.last_price > 0 && st.bal+cfg->external_balance > 0
			&& std::isfinite(st.val) && std::isfinite(st.neutral_price) && std::isfinite(st.power) && std::isfinite(st.bal) && std::isfinite(st.last_price) && std::isfinite(st.neutral_pos);
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcNewState(const PCalc &calc, const PConfig &cfg, State &nwst) {
	double adjbalance = std::abs(nwst.bal + cfg->external_balance) * cfg->power;
	nwst.power = calc->calcPower(nwst.last_price, adjbalance, cfg->asym);
	recalcNeutral(calc,cfg,nwst);
	for (int i = 0; i < 100; i++) {
		nwst.power = calc->calcPower(nwst.neutral_price, adjbalance, cfg->asym);
		recalcNeutral(calc,cfg,nwst);
	}
	nwst.val = calc->calcPosValue(nwst.power, calcAsym(cfg,nwst), nwst.neutral_price, nwst.last_price);
}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::init(const PCalc &calc, const PConfig &cfg, double price, double pos, double currency, const IStockApi::MarketInfo &minfo) {
	bool futures = minfo.leverage != 0 || cfg->longonly;
	auto bal = getBalance(*cfg,futures, price, pos, currency);
	State nwst {
		/*neutral_price:*/ price,
		/*last_price */ price,
		/*position */ pos - bal.second,
		/*bal */ bal.first-cfg->external_balance,
	};
	if (nwst.bal+cfg->external_balance<= 0) {
		//we cannot calc with empty balance. In this case, use price for calculation (but this is  unreal, trading still impossible)
		nwst.bal = price;
	}
	nwst.spot = minfo.leverage == 0;
	PCalc newcalc = calc;
	if (!newcalc->isValid(minfo)) newcalc = std::make_shared<Calc>(calc->init(minfo));
	recalcNewState(newcalc, cfg,nwst);

	auto res = PStrategy(new Strategy_Leveraged (newcalc, cfg, std::move(nwst)));
	if (!res->isValid())  {
		throw std::runtime_error("Unable to initialize strategy - invalid configuration");
	}
	return res;
}




template<typename Calc>
double Strategy_Leveraged<Calc>::calcPosition(double price) const {

	double reduction = cfg->reduction>=0?2*cfg->reduction:0;
	double dynred = 0;
	if (cfg->dynred) {
		double f;
		if (st.last_price > st.neutral_price) {
			f = st.last_price / st.neutral_price- 1.0;
		} else {
			f = st.neutral_price / st.last_price - 1.0;
		}
		dynred = std::min(1.0,f * cfg->dynred);
	}
	reduction = reduction + dynred;
	double new_neutral;


	double profit = st.position * (price - st.last_price);
	{
		//NOTE: always reduce when price is going up
		//because we need to reduce risk from short (so reduce when opening short position)
		//and we reduce opened long position as well
		//
		//for inverted futures, short and long is swapped
		if (reduction && st.position != 0 && st.last_dir) {
		//	profit += st.bal - st.redbal;
			new_neutral = calcNewNeutralFromProfit(profit, price,reduction);
		} else {
			new_neutral = st.neutral_price;
		}
	}



	double pos = calc->calcPosition(st.power, calcAsym(), new_neutral, price);
	if ((cfg->longonly || st.spot) && pos < 0) pos = 0;
	double pp = pos * st.position ;
	if (pp < 0) return 0;
	else if (pp == 0) {
		pos = pos * std::pow(2.0,cfg->initboost);
	}
	return pos;

}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::onIdle(
		const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets, double currency) const {
	if (isValid()) {
		if (st.power <= 0) {
			State nst = st;
			recalcNewState(calc, cfg, nst);
			return new Strategy_Leveraged<Calc>(calc, cfg, std::move(nst));
		} else {
			return this;
		}
	}
	else {
		return init(calc, cfg,ticker.last, assets, currency, minfo);
	}
}

template<typename Calc>
double Strategy_Leveraged<Calc>::calcNewNeutralFromProfit(double profit, double price, double reduction) const {

	double asym = calcAsym();
	double middle = calc->calcPrice0(st.neutral_price, asym);
	if ((middle - st.last_price ) * (middle - price) <= 0)
		return st.neutral_price;


	double new_val;
	bool rev_shift = ((price >= middle && price <= st.neutral_price) || (price <= middle && price >= st.neutral_price));
	double prev_val = st.val;
	new_val = prev_val - profit;
	double c_neutral;
	double neutral_from_price = calc->calcNeutralFromPrice0(price, asym);
	if (calc->calcPosValue(st.power, asym, neutral_from_price, price) > new_val) {
		c_neutral = neutral_from_price;
	} else {
		c_neutral = calc->calcNeutralFromValue(st.power, asym, st.neutral_price, new_val, price);
		if (rev_shift) {
			c_neutral = 2*st.neutral_price - c_neutral;
		}
	}

	double final_reduction;
	if (reduction <= 0.5) {
		if (profit > 0) final_reduction = reduction * 2; else final_reduction = 0;
	} else if (reduction <= 1 ){
		if (profit > 0) final_reduction = 1; else final_reduction = 2*(reduction-0.5);
	} else {
		if (profit >= 0) final_reduction = 2*reduction-1; else final_reduction = 1;
	}

	double new_neutral = st.neutral_price + (c_neutral - st.neutral_price) * (final_reduction);
	if ((new_neutral - price)*(st.neutral_price-price) < 0) new_neutral = price;
	return new_neutral;
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcPower(const PCalc &calc, const PConfig &cfg, State &nwst) {
	double offset = calc->calcPosition(nwst.power, calcAsym(cfg, nwst), nwst.neutral_price, nwst.neutral_price);
	double adjbalance = std::abs(nwst.bal  + cfg->external_balance + nwst.neutral_price * std::abs(nwst.position - offset) * cfg->powadj) * cfg->power;
	double power = calc->calcPower(nwst.neutral_price, adjbalance, cfg->asym);
	if (std::isfinite(power)) {
		nwst.power = power;
	}
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcNeutral(const PCalc &calc, const PConfig &cfg,State &nwst)  {
	double neutral_price = calc->calcNeutral(nwst.power, calcAsym(cfg,nwst), nwst.position,
			nwst.last_price);
	if (std::isfinite(neutral_price) && neutral_price > 0) {
		nwst.neutral_price = neutral_price;
	}
}

template<typename Calc>
std::pair<typename Strategy_Leveraged<Calc>::OnTradeResult, PStrategy> Strategy_Leveraged<Calc>::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {


	if (!isValid()) {
		return init(calc, cfg,tradePrice, assetsLeft, currencyLeft, minfo)
				->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);
	}

	State nwst = st;
	double neutral_recalc_ratio = 0;
	double apos = assetsLeft - st.neutral_pos;
	auto cpos = calcPosition(tradePrice);
//	auto vcpos = cpos;
	double calcSize = cpos - st.position;
	if (st.avgprice) {
		nwst.avgprice = std::exp((50*std::log(st.avgprice) + std::log(tradePrice))/51);
		nwst.neutral_pos = (tradePrice - st.avgprice)*cfg->trend_factor*(st.bal + cfg->external_balance)/pow2(st.avgprice);
	} else {
		nwst.avgprice = tradePrice;
		nwst.neutral_pos = 0;
	}


	if (calcSize * tradeSize > 0) { //differs from calculated size but in same direction {
		//to fight against partial execution. When execution is less then expected, adjust neutral_price accordingly
		neutral_recalc_ratio = std::min(1.0, pow2(tradeSize/calcSize));
	} else if (std::abs(cpos*tradePrice)/(st.bal+cfg->external_balance)>0.5) { //zero or reversed direction, test whether the position has a meaning
//		vcpos = st.position;   //don't change position
		neutral_recalc_ratio = 0; //don't change neutral price
	}
	double mult = st.power;
	double profit = (apos - tradeSize) * (tradePrice - st.last_price);
//	double vprofit = (st.position) * (tradePrice - st.last_price);
	//store current position
	nwst.position = cpos;
	//store last price
	nwst.last_price = tradePrice;
	nwst.last_dir = sgn(tradeSize);

	if (cpos == 0) {
		nwst.neutral_price = tradePrice;
		neutral_recalc_ratio = 1.0;
	} else if (cfg->reduction >= 0) {
		recalcNeutral(calc, cfg, nwst);
	} else {
		double ema = -cfg->reduction*200;
		nwst.neutral_price = std::exp((std::log(nwst.neutral_price) * (ema-1) + std::log(tradePrice))/ema);
	}
	nwst.neutral_price = st.neutral_price + (nwst.neutral_price - st.neutral_price)*neutral_recalc_ratio;

	double val = calc->calcPosValue(mult, calcAsym(), nwst.neutral_price, tradePrice);
	//calculate extra profit - we get change of value and add profit. This shows how effective is strategy. If extra is positive, it generates
	//profit, if it is negative, is losses
	double extra = (val - st.val) + profit;
	//store val to calculate next profit (because strategy was adjusted)
	nwst.val = val;


//	double baladj = (val - st.val) + profit;
//	double vbaladj = (val - st.val) + vprofit;

	if (cfg->reinvest_profit) {
		nwst.norm_profit += extra;
	}
	if (cpos == 0) {
		nwst.bal += nwst.norm_profit;
		nwst.norm_profit = 0;
	}
	nwst.position = cpos;

	/* else if (tradeSize == 0 && cpos && nwst.bal+cfg->external_balance+baladj > 0) {
		nwst.bal += baladj;
	}*/


	recalcPower(calc, cfg, nwst);

	return {
		OnTradeResult{extra,0,calc->calcPrice0(st.neutral_price, calcAsym()),0},
		new Strategy_Leveraged<Calc>(calc, cfg,  std::move(nwst))
	};

}

template<typename Calc>
json::Value Strategy_Leveraged<Calc>::storeCfgCmp() const {
	return json::Object("asym", static_cast<int>(cfg->asym * 1000))("ebal",
			static_cast<int>(cfg->external_balance * 1000))("power",
			static_cast<int>(cfg->power * 1000))("lo",cfg->longonly);
}

template<typename Calc>
json::Value Strategy_Leveraged<Calc>::exportState() const {
	return json::Object
			("neutral_price",st.neutral_price)
			("last_price",st.last_price)
			("position",st.position)
			("balance",st.bal)
			("val",st.val)
			("power",st.power)
			("neutral_pos",st.neutral_pos)
			("avgprice", st.avgprice)
			("last_dir", st.last_dir)
			("norm_profit", st.norm_profit)
			("cfg", storeCfgCmp());

}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::importState(json::Value src,const IStockApi::MarketInfo &minfo) const {
		State newst {
			src["neutral_price"].getNumber(),
			src["last_price"].getNumber(),
			src["position"].getNumber(),
			src["balance"].getNumber(),
			src["val"].getNumber(),
			src["norm_profit"].getNumber(),
			src["power"].getNumber(),
			src["neutral_pos"].getNumber(),
			src["avgprice"].getNumber(),
			src["last_dir"].getInt(),
			minfo.leverage == 0
		};
		json::Value cfgcmp = src["cfg"];
		json::Value cfgcmp2 = storeCfgCmp();
		if (cfgcmp != cfgcmp2) {
			double last_price = newst.last_price;
			if (cfg->recalc_keep_neutral) {
				newst.last_price = calc->calcPrice0(newst.neutral_price, calcAsym(cfg, newst));
				newst.position = 0;
				recalcNewState(calc, cfg,newst);
				newst.last_price = last_price;
				newst.position = calc->calcPosition(newst.power,calcAsym(cfg,newst),newst.neutral_price, last_price);
			} else {
				recalcNewState(calc, cfg,newst);
			}
			newst.val= calc->calcPosValue(newst.power,calcAsym(cfg,newst),newst.neutral_price, last_price);
		}
		PCalc newcalc = calc;
		if (!newcalc->isValid(minfo)) newcalc = std::make_shared<Calc>(newcalc->init(minfo));
		return new Strategy_Leveraged<Calc>(newcalc, cfg, std::move(newst));
}

template<typename Calc>
IStrategy::OrderData Strategy_Leveraged<Calc>::getNewOrder(
		const IStockApi::MarketInfo &minfo,
		double curPrice, double price, double dir, double assets, double currency, bool rej) const {
	auto apos = assets - st.neutral_pos;
	double asym = calcAsym(cfg,st);
	double bal = (st.bal+cfg->external_balance);;
	double lev = std::abs(st.position) * st.last_price / bal;
	if (!rej && ((lev > 0.5 && dir != st.last_dir) || lev>2)  && st.val > 0) {
		if (cfg->fastclose && dir * st.position < 0) {
			double midl = calc->calcPrice0(st.neutral_price, asym);
			double calc_price = (price - midl) * (st.last_price - midl) < 0?midl:price;
			double newval = calc->calcPosValue(st.power, asym,st.neutral_price, calc_price);
			double valdiff = st.val - newval;
			if (valdiff > 0) {
				double fastclose_delta = valdiff/st.position;
				double close_price = fastclose_delta+st.last_price;
				if (close_price * dir < curPrice * dir && close_price * dir > price * dir) {
					price = close_price;
				}
			}
		}
		if (cfg->slowopen && dir * st.position > 0) {
			double newval = calc->calcPosValue(st.power, asym,st.neutral_price, price);
			double valdiff = newval - st.val;
			if (valdiff > 0) {
				double delta = -valdiff/st.position;
				double open_price = delta + st.last_price;
				if (open_price * dir < price * dir &&  price > 0) {
					logDebug("Slow open active: valdiff=$1, delta=$2, spread=$3",
							valdiff, delta, price-st.last_price);
					price = open_price;
				}

			}
		}

	}


	auto cps = calcPosition(price);
	double df = cps - apos;
	double eq = getEquilibrium(assets);
	double pdif = price - st.last_price;
	if (df * dir < 0) {
		double cps2 = calc->calcPosition(st.power, calcAsym(), st.neutral_price, eq+pdif);
		double df2 = cps2 - apos;
		df = df2*pow2(cps)/pow2(std::abs(cps)+std::abs(cps2));
	} else if (st.last_dir && st.last_dir != dir) {
		double cps2 = calcPosition(eq+pdif);
		double df2 = cps2 - apos;
		df = df2;
	}
	if (!std::isfinite(df)) return {0,0,Alert::forced};
	double finpos = assets+df;
	if (finpos < 0 && (cfg->longonly || st.spot)) return {0,0,Alert::forced};
	auto alert = std::abs(finpos) <= 2*minfo.min_size || std::abs(finpos*price) < 2*minfo.min_volume?Alert::forced:Alert::enabled;
	return {price, df,  alert};
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::MinMax Strategy_Leveraged<Calc>::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	auto r = calcRoots();
	if (cfg->longonly || st.spot) {
		r.max = st.neutral_price;
	}
	return r;
}

template<typename Calc>
double Strategy_Leveraged<Calc>::getEquilibrium(double assets) const {
	return  calc->calcPriceFromPosition(st.power, calcAsym(), st.neutral_price, assets-st.neutral_pos);
}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::reset() const {
	return new Strategy_Leveraged<Calc>(calc, cfg,{});
}

template<typename Calc>
json::Value Strategy_Leveraged<Calc>::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	double bal = cfg->external_balance+st.bal;
	return json::Object("Position", (minfo.invert_price?-1:1)*st.position)
				  ("Last price", minfo.invert_price?1/st.last_price:st.last_price)
				 ("Neutral price", minfo.invert_price?1/st.neutral_price:st.neutral_price)
				 ("Value", st.val)
				 ("Collateral", bal)
				 ("Current leverage",  std::abs(st.position) * st.last_price / bal)
				 ("Multiplier", st.power)
				 ("Neutral pos", (minfo.invert_price?-1:1)*st.neutral_pos)
				 ("Norm profit", st.norm_profit)
	 	 	 	 ("Avg price", minfo.invert_price?1/st.avgprice:st.avgprice);


}




template<typename Calc>
typename Strategy_Leveraged<Calc>::MinMax Strategy_Leveraged<Calc>::calcRoots() const {
	if (!rootsCache.has_value()) {
		rootsCache = calc->calcRoots(st.power, calcAsym(),st.neutral_price, st.bal+cfg->external_balance);
	}
	return *rootsCache;
}

template<typename Calc>
inline double Strategy_Leveraged<Calc>::calcAsym(const PConfig &cfg, const State &st)  {
	return cfg->asym;
}

template<typename Calc>
inline double Strategy_Leveraged<Calc>::calcAsym() const {
	return calcAsym(cfg,st);
}


template<typename Calc>
std::pair<double,double> Strategy_Leveraged<Calc>::getBalance(const Config &cfg, bool leveraged, double price, double assets, double currency) {
	if (leveraged) {
		if (cfg.external_balance) return {cfg.external_balance, 0};
		else return {currency, 0};
	} else {
		double md = assets + currency / price;
		double bal = cfg.external_balance?cfg.external_balance:(md * price);
		return {bal, 0};
	}
}

template<typename Calc>
inline double Strategy_Leveraged<Calc>::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	if (!isValid()) {
		return init(calc, cfg, price, assets, currency, minfo)->calcInitialPosition(minfo,price,assets,currency);
	} else {
		return calcPosition(st.neutral_price);
	}
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::BudgetInfo Strategy_Leveraged<Calc>::getBudgetInfo() const {
	return {st.bal + cfg->external_balance, 0};
}


template<typename Calc>
inline double Strategy_Leveraged<Calc>::calcCurrencyAllocation(double) const {
	return cfg->external_balance + st.bal - st.val - (st.spot?st.position*st.last_price:0.0);
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::ChartPoint Strategy_Leveraged<Calc>::calcChart(double price) const {
	return {true,calc->calcPosition(st.power, calcAsym(cfg,st), st.neutral_price, price), cfg->external_balance+st.bal-calc->calcPosValue(st.power, calcAsym(cfg,st), st.neutral_price, price)};
}

