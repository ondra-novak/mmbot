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

#include "sgn.h"


using ondra_shared::logDebug;

template<typename Calc>
std::string_view Strategy_Leveraged<Calc>::id = Calc::id;

template<typename Calc>
Strategy_Leveraged<Calc>::Strategy_Leveraged(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}
template<typename Calc>
Strategy_Leveraged<Calc>::Strategy_Leveraged(const Config &cfg)
:cfg(cfg) {}


template<typename Calc>
bool Strategy_Leveraged<Calc>::isValid() const {
	return st.neutral_price > 0 && st.bal > 0;
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcNewState(const Config &cfg, State &nwst) {
	if (!std::isfinite(nwst.neutral_price) || nwst.neutral_price<=0) nwst.neutral_price = nwst.last_price;
	for (int i = 0; i < 16; i++) {
		recalcPower(cfg,nwst);
		recalcNeutral(cfg,nwst);
	}
	nwst.val = Calc::calcPosValue(nwst.power, cfg.asym, nwst.neutral_price, nwst.last_price);
}

template<typename Calc>
Strategy_Leveraged<Calc> Strategy_Leveraged<Calc>::init(const Config &cfg, double price, double pos, double currency, bool futures) {
	State nwst {
		/*neutral_price:*/ price,
		/*last_price */ price,
		/*position */ pos,
		/*bal */ futures?currency:(currency + pos * price),
		/* val */ 0,
		/* power */ 0
	};
	if (nwst.bal <= 0) {
		//we cannot calc with empty balance. In this case, use price for calculation (but this is  unreal, trading still impossible)
		nwst.bal = price;
	}
	recalcNewState(cfg,nwst);
	return Strategy_Leveraged(cfg, std::move(nwst));
}



template<typename Calc>
double Strategy_Leveraged<Calc>::calcMult() const {
	return st.power;
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::PosCalcRes Strategy_Leveraged<Calc>::calcPosition(double price) const {
	auto mm = calcRoots();
	bool lmt = false;
	if (price < mm.min) {price = mm.min; lmt = true;}
	if (price > mm.max) {price = mm.max; lmt = true;}
	if (lmt) {
		return {true,st.position};
	} else {

		double profit = st.position * (price - st.last_price);
		double new_neutral = cfg.reduction?calcNewNeutralFromProfit(profit, price):st.neutral_price;
		double pos = Calc::calcPosition(calcMult(), cfg.asym, new_neutral, price);
		return {false,pos};
	}
}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::onIdle(
		const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets, double currency) const {
	if (isValid()) {
		if (st.power <= 0) {
			State nst = st;
			recalcNewState(cfg, nst);
			return new Strategy_Leveraged<Calc>(cfg, std::move(nst));
		} else {
			return this;
		}
	}
	else return new Strategy_Leveraged<Calc>(init(cfg,ticker.last, assets, currency, minfo.leverage != 0));
}

template<typename Calc>
double Strategy_Leveraged<Calc>::calcNewNeutralFromProfit(double profit, double price) const {
	if ((st.last_price - st.neutral_price) * (price - st.neutral_price) <= 0 || profit == 0)
			return st.neutral_price;

	double mult = calcMult();
	double middle = Calc::calcPrice0(st.neutral_price, cfg.asym);
	double prev_val = st.val;//calcPosValue(mult, cfg.asym, st.neutral_price, st.last_price);
	double cur_val = Calc::calcPosValue(mult, cfg.asym, st.neutral_price, price);
	double new_val;
	if (prev_val < 0 && (price - middle) * (st.neutral_price - middle)>0) {
		new_val = 2*cur_val - (prev_val - profit);
	} else {
		new_val = prev_val - profit;
	}

	double new_neutral = st.neutral_price + (Calc::calcNeutralFromValue(mult, cfg.asym, st.neutral_price, new_val, price) - st.neutral_price)* 2 * (cfg.reduction+cfg.dynred*std::abs(st.position*st.last_price/st.bal));

	double pos1 = Calc::calcPosition(mult, cfg.asym, st.neutral_price, price);
	double pos2 = Calc::calcPosition(mult, cfg.asym, new_neutral, price);
	if ((pos1 - st.position) * (pos2 - st.position) < 0) {
		return Calc::calcNeutral(mult, cfg.asym, st.position, price);
	} else {
		return new_neutral;

	}
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcPower(const Config &cfg, State &nwst) {
	double offset = Calc::calcPosition(nwst.power, cfg.asym, nwst.neutral_price,
			nwst.neutral_price);
	double power = std::abs((nwst.bal+cfg.external_balance)/nwst.last_price + std::abs(nwst.position - offset) * cfg.powadj) * cfg.power;
	if (std::isfinite(power)) {
		nwst.power = power;
	}
}

template<typename Calc>
void Strategy_Leveraged<Calc>::recalcNeutral(const Config &cfg,State &nwst)  {
	double neutral_price = Calc::calcNeutral(nwst.power, cfg.asym, nwst.position,
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
		return init(cfg,tradePrice, assetsLeft, currencyLeft, minfo.leverage != 0)
				.onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);
	}

	State nwst = st;
	auto cpos = calcPosition(tradePrice);
	double mult = calcMult();
	double profit = (assetsLeft - tradeSize) * (tradePrice - st.last_price);
	double vprofit = (st.position) * (tradePrice - st.last_price);
	//store current position
	nwst.position = cpos.pos;
	//store last price
	nwst.last_price = tradePrice;

	recalcNeutral(cfg, nwst);

	double val = Calc::calcPosValue(mult, cfg.asym, nwst.neutral_price, tradePrice);
	//calculate extra profit - we get change of value and add profit. This shows how effective is strategy. If extra is positive, it generates
	//profit, if it is negative, is losses
	double extra = (val - st.val) + profit;
	double vextra = (val - st.val) + vprofit;

	//store val to calculate next profit (because strategy was adjusted)
	nwst.val = val;
	//store new balance
	nwst.bal = st.bal + vextra;

	recalcPower(cfg, nwst);

	return {
		OnTradeResult{extra,0,Calc::calcPrice0(st.neutral_price, cfg.asym),0},
		new Strategy_Leveraged<Calc>(cfg,  std::move(nwst))
	};

}

template<typename Calc>
json::Value Strategy_Leveraged<Calc>::storeCfgCmp() const {
	return json::Object("asym", static_cast<int>(cfg.asym * 1000))("ebal",
			static_cast<int>(cfg.external_balance * 1000))("power",
			static_cast<int>(cfg.power * 1000));
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
			("cfg", storeCfgCmp());

}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::importState(json::Value src) const {
		State newst {
			src["neutral_price"].getNumber(),
			src["last_price"].getNumber(),
			src["position"].getNumber(),
			src["balance"].getNumber(),
			src["val"].getNumber(),
			src["power"].getNumber(),
		};
		json::Value cfgcmp = src["cfg"];
		json::Value cfgcmp2 = storeCfgCmp();
		if (cfgcmp != cfgcmp2) {
			recalcNewState(cfg,newst);
		}
		return new Strategy_Leveraged<Calc>(cfg, std::move(newst));
}

template<typename Calc>
IStrategy::OrderData Strategy_Leveraged<Calc>::getNewOrder(
		const IStockApi::MarketInfo &,
		double curPrice, double price, double dir, double assets, double /*currency*/) const {
	auto mm = calcRoots();
	if (curPrice < mm.min || curPrice > mm.max) {
		if (dir * assets < 0) return {curPrice,-assets,Alert::stoploss};
		else return {0,0,Alert::forced};
	} else {
		auto cps = calcPosition(price);
		double ch1 = cps.pos - st.position;
		double ch2 = cps.pos - assets;
		if (ch2 * dir < 0)
			ch2 = ch1 / 2.0;
		else if (ch2 * dir > 2 * ch1 * dir)
			ch2 = ch1 * 2;
		return {0, ch2};
	}
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::MinMax Strategy_Leveraged<Calc>::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	return calcRoots();
}

template<typename Calc>
double Strategy_Leveraged<Calc>::getEquilibrium(double assets) const {
	return  Calc::calcPriceFromPosition(st.power, cfg.asym, st.neutral_price, assets);
}

template<typename Calc>
PStrategy Strategy_Leveraged<Calc>::reset() const {
	return new Strategy_Leveraged<Calc>(cfg,{});
}

template<typename Calc>
json::Value Strategy_Leveraged<Calc>::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Position", (minfo.invert_price?-1:1)*st.position)
				  ("Last price", minfo.invert_price?1/st.last_price:st.last_price)
				 ("Neutral price", minfo.invert_price?1/st.neutral_price:st.neutral_price)
				 ("Value", st.val)
				 ("Last balance", st.bal)
				 ("Multiplier", st.power);


}



template<typename Calc>
double Strategy_Leveraged<Calc>::calcMaxLoss() const {
	double lmt;
	if (cfg.max_loss == 0)
		lmt = st.bal;
	else
		lmt = cfg.max_loss;

	return lmt;
}

template<typename Calc>
typename Strategy_Leveraged<Calc>::MinMax Strategy_Leveraged<Calc>::calcRoots() const {
	if (!rootsCache.has_value()) {
		double lmt = calcMaxLoss();
		rootsCache = Calc::calcRoots(calcMult(), cfg.asym,st.neutral_price,lmt);
	}
	return *rootsCache;
}

template<typename Calc>
double Strategy_Leveraged<Calc>::adjNeutral(double price, double value) const {
	double mult = calcMult();
	auto fncalc = [&](double x) {
		return Calc::calcPosValue(mult,cfg.asym, x, price) - value;
	};
	double m = Calc::calcPrice0(st.neutral_price, cfg.asym);
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

