/*
 * strategy_pile.cpp
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#include <imtjson/object.h>
#include "strategy_pile.h"
#include <stdexcept>
#include <cmath>
Strategy_Pile::Strategy_Pile(const Config &cfg):cfg(cfg) {}
Strategy_Pile::Strategy_Pile(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Pile::id = "pile";

PStrategy Strategy_Pile::init(double price, double assets, double currency, bool leveraged) const {
    double equity = (leveraged?0:(price*assets))+currency;

    double kmult = calcKMult(price, equity, cfg.ratio);
    PStrategy out(new Strategy_Pile(cfg, State{
        price,
        kmult,
        equity
    }));
    if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
    return out;


/*

	double v = price*assets;
	double b = leveraged?currency:(v+currency);
	double r = v/b;
	if (r <= 0.001) throw std::runtime_error("Unable to initialize strategy - you need to buy some assets");
	if (r > 0.999)  throw std::runtime_error("Unable to initialize strategy - you need to have some currency");
	double m = assets/calcPosition(r, 1, price);
	double cb = calcBudget(r, m, price);
	PStrategy out(new Strategy_Pile(cfg, State{
			r, //ratio
			m, //kmult
			price, //last price
			cb, //budget
			assets,
			0,
	}));
	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
	*/
}

double Strategy_Pile::calcKMult(double price, double budget, double ratio) {
    double c = calcBudget(ratio, 1, price);
    double kmult = budget/c;
    return kmult;
}

double Strategy_Pile::calcPosition(double ratio, double kmult, double price) {
	return kmult*std::pow(price, ratio-1);
}

double Strategy_Pile::calcBudget(double ratio, double kmult, double price) {
	return kmult*std::pow(price,ratio)/(ratio);
}

double Strategy_Pile::calcEquilibrium(double ratio, double kmul, double position) {
	return std::pow<double>(position/kmul,-1.0/(1-ratio));
}

double Strategy_Pile::calcPriceFromBudget(double ratio, double kmul, double budget) {
	return std::pow(budget*ratio/kmul, 1.0/(ratio));
}

double Strategy_Pile::calcCurrency(double ratio, double kmult, double price) {
	return kmult*(std::pow(price,ratio)/(ratio) - std::pow(price, ratio-1)*price);
}

double Strategy_Pile::calcPriceFromCurrency(double ratio, double kmult, double currency) {
	return std::pow(-(kmult - kmult/ratio)/currency,-1.0/ratio);
}



IStrategy::OrderData Strategy_Pile::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	double finPos = calcPosition(cfg.ratio, st.kmult, new_price);
	double finPl = (new_price - st.lastp) * assets;
	double finBudget = calcBudget(cfg.ratio, st.kmult, new_price);
	double np = finPl -  (finBudget - st.budget);
	double accum = cfg.accum * np/new_price;
	finPos += accum;
	double diff = finPos - assets;
	return {0, diff};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Pile::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {
	if (!isValid()) return
			this->init(tradePrice, assetsLeft-tradeSize, currencyLeft, minfo.leverage != 0)
				 ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

	double prevpos = assetsLeft - tradeSize;
	double plchange = (tradePrice - st.lastp) * prevpos;
	double preveq = st.budget;
	double cureq = calcBudget(cfg.ratio, st.kmult, tradePrice);
	double budgetchange = cureq - preveq;
	double np = plchange - budgetchange;

	double accum = np/tradePrice * cfg.accum;
	np = np * (1-cfg.accum);

	return {
		{np, accum,0,0},
		PStrategy(new Strategy_Pile(cfg, State {
		    tradePrice, st.kmult, cureq
		}))
	};

}

PStrategy Strategy_Pile::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State nst;
    nst.budget = src["budget"].getNumber();
    nst.lastp = src["lastp"].getNumber();
    nst.kmult = calcKMult(nst.lastp, nst.budget, cfg.ratio);
    return new Strategy_Pile(cfg, std::move(nst));
}

IStrategy::MinMax Strategy_Pile::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	double  pos = calcPosition(cfg.ratio, st.kmult, st.lastp);
	MinMax r;
	if (pos > assets) {
		r.max = calcEquilibrium(cfg.ratio, st.kmult, pos - assets);
	} else {
		r.max = std::numeric_limits<double>::infinity();
	}
	double cur = calcCurrency(cfg.ratio, st.kmult, st.lastp);
	double avail = currencies  + (assets>pos?(assets-pos)*st.lastp:0);
	if (cur > avail) {
		r.min = calcPriceFromCurrency(cfg.ratio, st.kmult, cur-avail);
	} else {
		r.min = 0;
	}
	return r;
}

bool Strategy_Pile::isValid() const {
	return st.budget > 0 && st.kmult > 0 && st.lastp > 0;
}

json::Value Strategy_Pile::exportState() const {
	return json::Object {
		{"lastp",st.lastp},
		{"budget",st.budget},
	};
}

std::string_view Strategy_Pile::getID() const {
	return id;
}

double Strategy_Pile::getCenterPrice(double lastPrice, double assets) const {
	return getEquilibrium(assets);
}

double Strategy_Pile::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	double budget = minfo.leverage?currency:(currency+price*assets);
	return (budget * cfg.ratio)/price;
}

IStrategy::BudgetInfo Strategy_Pile::getBudgetInfo() const {
	return {
		calcBudget(cfg.ratio, st.kmult, st.lastp),
		calcPosition(cfg.ratio, st.kmult, st.lastp)
	};
}

double Strategy_Pile::getEquilibrium(double assets) const {
	return calcEquilibrium(cfg.ratio, st.kmult, assets);
}

double Strategy_Pile::calcCurrencyAllocation(double price, bool leveraged) const {
    if (leveraged) return calcBudget(cfg.ratio,st.kmult, price);
	return calcCurrency(cfg.ratio,st.kmult, st.lastp);
}

IStrategy::ChartPoint Strategy_Pile::calcChart(double price) const {
	return ChartPoint{
		true,
		calcPosition(cfg.ratio, st.kmult, price),
		calcBudget(cfg.ratio, st.kmult, price)
	};
}

PStrategy Strategy_Pile::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Pile::reset() const {
	return new Strategy_Pile(cfg);
}

json::Value Strategy_Pile::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object{
		{"Budget",st.budget},
		{"Multiplier",st.kmult},
	};
}

