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
	logInfo("init: price=$1, assets=$2, currency=$3", price, assets, currency);

	// Start with asset allocation as reported by mtrader, assume current price as enter price
	PStrategy out(new Strategy_Epa(cfg, State{
			assets * price, //ep
			assets > 0 ? price : std::numeric_limits<double>::quiet_NaN(), //enter
			currency + (assets * price), // budget
			assets,
			currency
	}));

	if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy - failed to validate state");
	return out;
}

double Strategy_Epa::calculateSize(double price, double assets) const {
	double effectiveAssets = std::min(st.assets, assets);

	double size;
	if (std::isnan(st.enter) || (effectiveAssets * price) < st.budget * cfg.min_asset_perc_of_budget) {
		// buy
		size = (st.budget * cfg.initial_bet_perc_of_budget) / price;
	}	else if (price < st.enter) {
		if (st.currency / price < effectiveAssets) {
			// sell
			size = (((st.currency / price) + effectiveAssets) * 0.5) - effectiveAssets;
		} else {
			// buy
			double angleRad = cfg.angle * M_PI / 180;
			double sqrtTan = std::sqrt(std::tan(angleRad));
			double cost = std::sqrt(st.ep) / sqrtTan;
			double candidateSize = cost / price;

			double dist = (st.enter - price) / st.enter;
			double norm = dist / cfg.max_enter_price_distance;
			double power = std::min(std::pow(norm, 4) * cfg.power_mult, cfg.power_cap);
			double newSize = candidateSize * power;

			size = std::isnan(newSize) ? 0 : std::max(0.0, newSize);
		}
	} else {
		// sell
		double dist = (price - st.enter) / price;
		double norm = dist / cfg.target_exit_price_distance;
		double power = std::pow(norm, 4) * cfg.exit_power_mult;
		size = -assets * power;
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

	return size;
}

IStrategy::OrderData Strategy_Epa::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {
	logInfo("getNewOrder: new_price=$1, assets=$2, currency=$3, dir=$4", new_price, assets, currency, dir);

	double size = calculateSize(new_price, assets);

	logInfo("   -> $1", size);

	// price where order is put. If this field is 0, recommended price is used
  // size of the order, +buy, -sell. If this field is 0, the order is not placed
	return { 0, size };
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Epa::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) throw std::runtime_error("Strategy is not initialized in onTrade call.");

	auto effectiveSize = tradeSize;

	// if the strategy is missing assets, consume to correct the state
	if (tradeSize > 0 && st.assets > assetsLeft - tradeSize) {
		effectiveSize = std::max(assetsLeft - st.assets, 0.0);
	}

	logInfo("onTrade: tradeSize=$1, assetsLeft=$2, currencyLeft=$3", tradeSize, assetsLeft, currencyLeft);
	
	auto newAsset = st.assets + effectiveSize;
	auto cost = tradePrice * effectiveSize;
	auto norm_profit = effectiveSize >= 0 ? 0 : (tradePrice - st.enter) * -effectiveSize;
	auto ep = effectiveSize >= 0 ? st.ep + cost : (st.ep / st.assets) * newAsset;
	auto enter = ep / newAsset;

	//logInfo("onTrade: tradeSize=$1, assetsLeft=$2, enter=$3, currencyLeft=$4", tradeSize, assetsLeft, enter, currencyLeft);

	return {
		// norm. p, accum, neutral pos, open price
		{ norm_profit, 0, std::isnan(enter) ? 0 : enter, 0 },
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
	MinMax range;
	range.min = 0;
	range.max = std::numeric_limits<double>::infinity();
	return range;
}

bool Strategy_Epa::isValid() const {
	return st.ep >= 0 && !std::isnan(st.budget) && st.budget > 0;
}

json::Value Strategy_Epa::exportState() const {
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

	if (std::isnan(st.enter)) {
		logInfo("getCenterPrice: lastPrice=$1, assets=$2 -*> $3", lastPrice, assets, lastPrice);
		return lastPrice;
	}

	double effectiveAssets = std::min(st.assets, assets);
	double center = st.currency / effectiveAssets;
	center = std::min(st.enter, center);

	logInfo("getCenterPrice: lastPrice=$1, assets=$2 -> $3", lastPrice, assets, center);

	return center;

	// st.currency / affectiveAssets = X

	//st.currency / new_price < effectiveAssets
	// what the point when to buy and when it is sell all way up?

	//return getEquilibrium(assets);
	// if (std::isnan(st.enter) || (st.assets * lastPrice) < st.budget * cfg.min_asset_perc_of_budget) {
	// 	return lastPrice;
	// }

	// todo: depending on currency?
	//return st.enter;
}

double Strategy_Epa::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
	// OK
	double budget = minfo.leverage ? currency : (currency + price * assets);
	return (budget * cfg.initial_bet_perc_of_budget) / price;
}

IStrategy::BudgetInfo Strategy_Epa::getBudgetInfo() const {
	return {
		st.budget, //calcBudget(st.ratio, st.kmult, st.lastp), // total
		0 //->last price st.assets + calculateSize(new_price, st.assets) //calcPosition(st.ratio, st.kmult, st.lastp) // assets
	};
}

double Strategy_Epa::getEquilibrium(double assets) const {
	// for UI
	return st.enter;
}

double Strategy_Epa::calcCurrencyAllocation(double price) const {
	// this is allocation that strategy wants for the given price
	return st.budget;
}

IStrategy::ChartPoint Strategy_Epa::calcChart(double price) const {
	double size = calculateSize(price, st.assets);
	
	return ChartPoint{
		true, //true
		st.assets + size, //calcPosition(st.ratio, st.kmult, price),
		st.currency - (size * price) //st.budget, //calcBudget(st.ratio, st.kmult, price)
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
	return json::Object{
		{"Enter price sum", st.ep},
		{"Enter price", st.enter},
		{"Budget", st.budget},
		{"Assets", st.assets},
		{"Currency", st.currency}
	};
}
