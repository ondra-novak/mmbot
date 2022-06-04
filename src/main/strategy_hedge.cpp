/*
 * strategy_hedge.cpp
 *
 *  Created on: 28. 5. 2021
 *      Author: ondra
 */

#include "strategy_hedge.h"

const std::string_view Strategy_Hedge::id="hedge";


Strategy_Hedge::Strategy_Hedge(const Config &cfg):cfg(cfg) {
}

Strategy_Hedge::Strategy_Hedge(const Config &cfg, State &&state):cfg(cfg),state(std::move(state)) {
}

IStrategy::OrderData Strategy_Hedge::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double df = state.position - assets;
	df = std::max(0.0,df*dir) * dir;
	return {cur_price, df, Alert::disabled};

}

std::pair<IStrategy::OnTradeResult, PStrategy > Strategy_Hedge::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	return {
		{0,0,tradePrice<(state.low+state.high)*0.5?state.low:state.high},
		this
	};
}

PStrategy Strategy_Hedge::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	return new Strategy_Hedge(cfg, State{
		src["p"].getNumber(),
		src["l"].getNumber(),
		src["h"].getNumber(),
		static_cast<int>(src["ffs"].getInt()),
	});
}

IStrategy::MinMax Strategy_Hedge::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {
	return {0,std::numeric_limits<double>::infinity()};
}

bool Strategy_Hedge::isValid() const {
	return state.high > 0 && state.low > 0;
}

json::Value Strategy_Hedge::exportState() const {
	using namespace json;
	return Value(object,{
			Value("p", state.position),
			Value("h", state.high),
			Value("l", state.low),
			Value("ffs", state.ffs),
	});
}

std::string_view Strategy_Hedge::getID() const {
	return id;
}

double Strategy_Hedge::getCenterPrice(double lastPrice, double assets) const {
	return lastPrice;
}

double Strategy_Hedge::calcInitialPosition(const IStockApi::MarketInfo &minfo,
		double price, double assets, double currency) const {
	return 0;
}

IStrategy::BudgetInfo Strategy_Hedge::getBudgetInfo() const {
	return {0,state.position};
}

double Strategy_Hedge::getEquilibrium(double assets) const {
	return state.last_price;
}

double Strategy_Hedge::calcCurrencyAllocation(double price, bool leveraged) const {
	return 0;
}

IStrategy::ChartPoint Strategy_Hedge::calcChart(double price) const {
	return {false,0,0};
}

PStrategy Strategy_Hedge::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {


	double low_ret = curTicker.last*(1+cfg.ptc_drop);
	double high_ret = curTicker.last*(1-cfg.ptc_drop);
	State nwst(state);
	if (nwst.high == 0 && nwst.low == 0) nwst.high = nwst.low = curTicker.last;
	if (nwst.low > low_ret) nwst.low = low_ret;
	if (nwst.high < high_ret) nwst.high = high_ret;
	if (curTicker.last < nwst.low) {
		nwst.ffs = -1;
		if (nwst.position>0) nwst.position = 0;
	} else if (curTicker.last > nwst.high) {
		if (nwst.position<0) nwst.position = 0;
		else if (minfo.leverage == 0 && cfg.h_short) {
			if (nwst.ffs != 1) {
				nwst.position = currency/curTicker.last;
			}
		}
		nwst.ffs = 1;
	} else {
		if (nwst.ffs<0) {
			if (cfg.h_long) nwst.position = currency/curTicker.last;
		}else if (nwst.ffs>0) {
			if (cfg.h_short) {
				if (minfo.leverage == 0) nwst.position = 0;
				else nwst.position = -currency/curTicker.last;
			}
		}
		nwst.ffs = 0;
	}
	nwst.last_price = curTicker.last;
	return new Strategy_Hedge(cfg,std::move(nwst));
}

PStrategy Strategy_Hedge::reset() const {
	return new Strategy_Hedge(cfg);
}

json::Value Strategy_Hedge::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {


	using namespace json;
	return Value(object,{
			Value("High",minfo.invert_price?1.0/state.low:state.high),
			Value("Low",minfo.invert_price?1.0/state.high:state.low),
			Value("Position",(minfo.invert_price?-1:1)*state.position),
			Value("FFS",(minfo.invert_price?-1:1)*state.ffs)
	});

}
