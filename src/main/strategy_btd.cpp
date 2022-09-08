/*
 * btd.cpp
 *
 *  Created on: 11. 02. 2021
 *      Author: matasx
 */

#include <imtjson/object.h>
#include "strategy_btd.h"

#include <cmath>

#include "sgn.h"
#include "../shared/logOutput.h"
using ondra_shared::logInfo;

Strategy_Btd::Strategy_Btd(const Config &cfg):cfg(cfg) {}
Strategy_Btd::Strategy_Btd(const Config &cfg, State &&st):cfg(cfg),st(std::move(st)) {}

std::string_view Strategy_Btd::id = "buy_the_dip";

PStrategy Strategy_Btd::init(double price, double assets, double currency, bool leveraged) const {
	logInfo("init: price=$1, assets=$2, currency=$3", price, assets, currency);

	// Start with asset allocation as reported by mtrader, assume current price as enter price
	PStrategy out(new Strategy_Btd(cfg, State{
			currency + (assets * price), // budget
			assets,
			currency,
			price
	}));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

std::pair<double, bool> Strategy_Btd::calculateSize(double price, double assets) const {
	double effectiveAssets = std::min(st.assets, assets);

	double size;
	bool alert = false;	
	if (!cfg.sell && (std::isnan(st.last_price) || st.last_price <= 0 || price < st.last_price)) {
		size = cfg.buy_currency_step / price;
	} else if (cfg.sell && (std::isnan(st.last_price) || st.last_price <= 0 || price > st.last_price)) {
		size = cfg.sell_asset_step;
	} else {
		size = 0;
	}

	// correction for missing assets on exchange
	if (size > 0 && size < st.assets - assets) {
		size = st.assets - assets;
	}

	// explicitly trade only within budget
	if (size > 0) {
		size = std::min(size, st.currency / price);
	} else {
		size = std::max(size, -effectiveAssets);
	}

	return {size, alert};
}

IStrategy::OrderData Strategy_Btd::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	auto res = calculateSize(new_price, assets);
	double size = res.first;
	auto alert = res.second ? IStrategy::Alert::enabled : IStrategy::Alert::disabled;

	// price where order is put. If this field is 0, recommended price is used
  // size of the order, +buy, -sell. If this field is 0, the order is not placed
	return { 0, size, alert };
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Btd::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) throw std::runtime_error("Strategy is not initialized in onTrade call.");

	auto effectiveSize = tradeSize;

	// if the strategy is missing assets, consume to correct the state
	if (tradeSize > 0 && st.assets > assetsLeft - tradeSize) {
		effectiveSize = std::max(assetsLeft - st.assets, 0.0);
	}	

	auto cost = tradePrice * effectiveSize;
	auto newAsset = st.assets + effectiveSize;

	return {
		// norm. p, accum, neutral pos, open price
		{ 0, 0, tradePrice, 0 },
		PStrategy(new Strategy_Btd(cfg, State { st.budget, newAsset, std::min(st.budget, st.currency - cost), tradeSize == 0 ? st.last_price : tradePrice }))
	};

}

PStrategy Strategy_Btd::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	State st {
			src["budget"].getNumber(),
			src["assets"].getNumber(),
			src["currency"].getNumber(),
			src["last_price"].getNumber()
	};
	return new Strategy_Btd(cfg, std::move(st));
}

IStrategy::MinMax Strategy_Btd::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const {
	MinMax range;
	range.min = 0;
	range.max = std::numeric_limits<double>::infinity();
	return range;
}

bool Strategy_Btd::isValid() const {
	return !std::isnan(st.budget) && st.budget > 0;
}

json::Value Strategy_Btd::exportState() const {
	return json::Object {
		{"budget", st.budget},
		{"assets", st.assets},
		{"currency", st.currency},
		{"last_price", st.last_price},
	};
}

std::string_view Strategy_Btd::getID() const {
	return id;
}

double Strategy_Btd::getCenterPrice(double lastPrice, double assets) const {
	// center price for spreads
	
	return (!std::isnan(st.last_price) && st.last_price > 0) ? st.last_price : lastPrice;

	// double effectiveAssets = std::min(st.assets, assets);
	// if (std::isnan(st.enter) || (effectiveAssets * lastPrice) < st.budget * cfg.min_asset_perc_of_budget) {
	// 	// make sure we can buy at any price for backtest
	// 	double cp = cfg.backtest ? 2 * lastPrice : lastPrice;
	// 	logInfo("getCenterPrice: lastPrice=$1, assets=$2 -*> $3", lastPrice, assets, cp);
	// 	return cp;
	// }

	// double availableCurrency = std::max(0.0, st.currency - (st.budget * cfg.dip_rescue_perc_of_budget));
	// double dist = (st.enter - lastPrice) / st.enter;
	// if (dist >= cfg.dip_rescue_enter_price_distance) {
	// 	availableCurrency = st.currency;
	// }
	// double center = availableCurrency * cfg.reduction_midpoint / effectiveAssets / (1 - cfg.reduction_midpoint);
	// center = std::min(st.enter, center);

	// logInfo("getCenterPrice: lastPrice=$1, assets=$2 -> $3", lastPrice, assets, center);

	// return center;
}

double Strategy_Btd::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	// OK
	return cfg.sell ? assets : (cfg.buy_currency_step / price);
}

IStrategy::BudgetInfo Strategy_Btd::getBudgetInfo() const {
	return {
		st.budget, //calcBudget(st.ratio, st.kmult, st.lastp), // total
		0 //->last price st.assets + calculateSize(new_price, st.assets) //calcPosition(st.ratio, st.kmult, st.lastp) // assets
	};
}

double Strategy_Btd::getEquilibrium(double assets) const {
	// for UI
	return st.last_price;
}

double Strategy_Btd::calcCurrencyAllocation(double price, bool) const {
	// this is allocation that strategy wants for the given price
	return st.budget;
}

IStrategy::ChartPoint Strategy_Btd::calcChart(double price) const {
	double size = calculateSize(price, st.assets).first;
	
	return ChartPoint{
		true, //true
		st.assets + size, //calcPosition(st.ratio, st.kmult, price),
		st.budget, //calcBudget(st.ratio, st.kmult, price)
	};
}

PStrategy Strategy_Btd::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (!isValid()) return init(curTicker.last, assets, currency, minfo.leverage != 0);
	else return this;
}

PStrategy Strategy_Btd::reset() const {
	return new Strategy_Btd(cfg);
}

json::Value Strategy_Btd::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object{
		{"Budget", st.budget},
		{"Assets", st.assets},
		{"Currency", st.currency},
		{"Last price", st.last_price}
	};
}
