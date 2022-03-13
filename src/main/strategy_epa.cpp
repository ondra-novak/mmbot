/*
 * strategy_epa.cpp
 *
 *  Created on: 11. 02. 2021
 *      Author: matasx
 */

#include <imtjson/object.h>
#include "strategy_epa.h"

#include <cmath>

#include "sgn.h"
#include "../shared/logOutput.h"
using ondra_shared::logInfo;

Strategy_Epa::Strategy_Epa(const Config &cfg):cfg(cfg) {}
Strategy_Epa::Strategy_Epa(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Epa::id = "enter_price_angle";

PStrategy Strategy_Epa::init(double price, double assets, double currency, bool leveraged) const {
	// double v = price*assets;
	// double b = leveraged?currency:(v+currency);
	// double r = v/b;
	// if (r <= 0.001) throw std::runtime_error("Unable to initialize strategy - you need to buy some assets");
	// if (r > 0.999)  throw std::runtime_error("Unable to initialize strategy - you need to have some currency");
	// double m = assets/calcPosition(r, 1, price);
	// double cb = calcBudget(r, m, price);

	logInfo("init: price=$1, assets=$2, currency=$3", price, assets, currency);

	// Start with assets on exchange, assume current price as enter price
	PStrategy out(new Strategy_Epa(cfg, State{
			assets * price, //ep
			assets > 0 ? price : std::numeric_limits<double>::quiet_NaN(), //enter
			// MTrade -> onInit(alocated) -> strategie -> calcCurrenctAllocation-> available
			currency + (assets * price), // budget // ??? + (assets * price)
			assets,
			currency
	}));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

IStrategy::OrderData Strategy_Epa::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {
	logInfo("getNewOrder: assets=$1, currency=$2", assets, currency);
	// OK

	// double finPos = calcPosition(st.ratio, st.kmult, new_price);
	// double accum = calcAccum(new_price).second;
	// finPos += accum;
	// double diff = finPos - assets;

	double size;
	if (std::isnan(st.enter) || (assets * new_price) < st.budget * cfg.min_asset_perc_of_budget) {
		size = (st.budget * cfg.initial_bet_perc_of_budget) / new_price;
	}	else if (new_price < st.enter) {
		if (st.currency / new_price < assets) {
			size = (((st.currency / new_price) + assets) * 0.5) - assets;
		} else {
			double angleRad = cfg.angle * M_PI / 180;
			double sqrtTan = std::sqrt(std::tan(angleRad));
			double cost = std::sqrt(st.ep) / sqrtTan;
			double candidateSize = cost / new_price;

			double dist = (st.enter - new_price) / st.enter;
			double norm = dist / cfg.max_enter_price_distance;
			double power = std::min(std::pow(norm, 4) * cfg.power_mult, cfg.power_cap);
			double newSize = candidateSize * power;

			size = std::isnan(newSize) ? 0 : std::max(0.0, newSize);
		}
	} else {
		double dist = (new_price - st.enter) / new_price;
		double norm = dist / cfg.target_exit_price_distance;
		double power = std::pow(norm, 4) * cfg.exit_power_mult;
		size = -assets * power;
	}

	// if (sgn(dir) != sgn(size)) {
	// 	logInfo("getNewOrder - alert: size=$1, dir=$2", size, dir);
	// }

	//logInfo("getNewOrder: size=$1, dir=$2", size, dir);

	// price where order is put. If this field is 0, recommended price is used
  // size of the order, +buy, -sell. If this field is 0, the order is not placed
	size += st.assets - assets;

	// explicitly trade only within budget
	if (size > 0) {
		size = std::min(size, st.currency / new_price);
	} else {
		size = std::max(size, st.currency / -new_price);
	}

	return { 0, size };
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Epa::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) throw std::runtime_error("Strategy is not initialized in onTrade call.");

	auto effectiveSize = tradeSize;

	if (tradeSize > 0 && st.assets > assetsLeft - tradeSize) {
		effectiveSize = std::max(assetsLeft - st.assets, 0.0);
	}

	logInfo("onTrade: tradeSize=$1, assetsLeft=$2, currencyLeft=$3", tradeSize, assetsLeft, currencyLeft);

	// auto normp = calcAccum(tradePrice);
	// auto cass = calcPosition(st.ratio, st.kmult, tradePrice);
	// auto diff = assetsLeft-cass-normp.second;
	
	auto newAsset = st.assets + effectiveSize;
	auto cost = tradePrice * effectiveSize;
	auto norm_profit = effectiveSize >= 0 ? 0 : (tradePrice - st.enter) * -effectiveSize;
	auto ep = effectiveSize >= 0 ? st.ep + cost : (st.ep / st.assets) * newAsset;
	auto enter = ep / newAsset;

	//logInfo("onTrade: tradeSize=$1, assetsLeft=$2, enter=$3, currencyLeft=$4", tradeSize, assetsLeft, enter, currencyLeft);

	return {
		// norm. p, accum, neutral pos, open price
		{ norm_profit, 0, 0, 0 },
		PStrategy(new Strategy_Epa(cfg, State { ep, enter, st.budget, newAsset, st.currency - cost }))
	};

}

PStrategy Strategy_Epa::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State st {
			src["ep"].getNumber(),
			src["enter"].getNumber(),
			src["budget"].getNumber(),
			src["assets"].getNumber(),
			src["currency"].getNumber()
	};
	return new Strategy_Epa(cfg, std::move(st));
}

IStrategy::MinMax Strategy_Epa::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	// for UI
	// calcSafeRange kdyžtak jako min vracej 0 a jako max dej std::numeric_limits<double>::infinite()

	MinMax range;
	range.min = 0;
	range.max = std::numeric_limits<double>::infinity();
	return range;

	// double pos = calcPosition(st.ratio, st.kmult, st.lastp);
	// MinMax r;
	// if (pos > assets) {
	// 	r.max = calcEquilibrium(st.ratio, st.kmult, pos - assets);
	// } else {
	// 	r.max = std::numeric_limits<double>::infinity();
	// }
	// double cur = calcCurrency(st.ratio, st.kmult, st.lastp);
	// double avail = currencies  + (assets>pos?(assets-pos)*st.lastp:0);
	// if (cur > avail) {
	// 	r.min = calcPriceFromCurrency(st.ratio, st.kmult, cur-avail);
	// } else {
	// 	r.min = 0;
	// }
	// return r;
}

bool Strategy_Epa::isValid() const {
	return st.ep >= 0 && !std::isnan(st.budget) && st.budget > 0;
}

json::Value Strategy_Epa::exportState() const {
	// OK
	return json::Object {
		{"ep", st.ep},
		{"enter", st.enter},
		{"budget", st.budget},
		{"assets", st.assets},
		{"currency", st.currency}
	};
}

std::string_view Strategy_Epa::getID() const {
	return id;
}

double Strategy_Epa::getCenterPrice(double lastPrice, double assets) const {
	// center price for spreads
	//return getEquilibrium(assets);
	if (std::isnan(st.enter) || (assets * lastPrice) < st.budget * cfg.min_asset_perc_of_budget) {
		return lastPrice;
	}
	// todo: depending on currency?
	return st.enter;
}

double Strategy_Epa::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	// OK
	double budget = minfo.leverage ? currency : (currency + price * assets);
	return (budget * cfg.initial_bet_perc_of_budget) / price;
}

IStrategy::BudgetInfo Strategy_Epa::getBudgetInfo() const {
	return {
		st.budget, //calcBudget(st.ratio, st.kmult, st.lastp), // total
		0 //calcPosition(st.ratio, st.kmult, st.lastp) // assets
	};
}

double Strategy_Epa::getEquilibrium(double assets) const {
	// for UI
	return st.enter;
	//return calcEquilibrium(st.ratio, st.kmult, assets);
}

double Strategy_Epa::calcCurrencyAllocation(double price) const {
	// this is allocation that strategy wants for the given price
	return st.budget;
	//return calcCurrency(st.ratio,st.kmult, st.lastp) +  st.berror;
}

IStrategy::ChartPoint Strategy_Epa::calcChart(double price) const {
	return ChartPoint{
		false, //true
		0, //calcPosition(st.ratio, st.kmult, price),
		0, //calcBudget(st.ratio, st.kmult, price)
	};
}

PStrategy Strategy_Epa::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Epa::reset() const {
	return new Strategy_Epa(cfg);
}

json::Value Strategy_Epa::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	// OK
	return json::Object{
		{"Enter price sum", st.ep},
		{"Enter price", st.enter},
		{"Budget", st.budget},
		{"Assets", st.assets},
		{"Currency", st.currency}
	};
}
