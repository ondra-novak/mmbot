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

Strategy_Epa::SizeResult Strategy_Epa::calculateSize(double cur_price, double price, double assets) const {
	double effectiveAssets = std::min(st.assets, assets);
	double availableCurrency = std::max(0.0, st.currency - (st.budget * cfg.dip_rescue_perc_of_budget));

	double size;
	double target_price = 0;
	bool alert = false;
	if (std::isnan(st.enter) || (effectiveAssets * price) < st.budget * cfg.min_asset_perc_of_budget * (1 - cfg.dip_rescue_perc_of_budget)) {
		// buy
		if (price > st.last_price) {
			// Move last price up with alert, unless downtrend mode is enabled
			alert = !cfg.downtrend;
			size = 0;
		} else if (st.sentiment > 0) {
			// Move last price up or down with alert due to uptrend sentiment
			alert = true;
			size = 0;
		} else {
			size = (availableCurrency * cfg.initial_bet_perc_of_budget) / price;
			if (st.alerts > 0) {
				size *= 0.5;
			}
		}
	}	else if (price < st.enter) {
		double dist = (st.enter - price) / st.enter;
		if (dist >= cfg.dip_rescue_enter_price_distance) {
			availableCurrency = st.currency;
		}

		double half = ((availableCurrency / price) + effectiveAssets) * cfg.reduction_midpoint;
		double hhSize = half - effectiveAssets;
		
		if (half < effectiveAssets) {
			// sell
			size = hhSize;
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

			size = std::isnan(newSize) ? 0 : std::max(0.0, std::min(hhSize, newSize));
		}
	} else {
		// sell
		double maxEp = (st.ep + availableCurrency) * cfg.enter_price_max_asset;
		double dist = (price - st.enter) / price;		
		double reduceTarget = cfg.target_exit_price_distance * 0.5;
		if (dist < reduceTarget) {
			// Reduce to maximum allowed held assets on enter price
			if (st.ep > maxEp) {
				size = (maxEp - st.ep) / st.enter;
			} else {
				size = 0;
			}
		} else if (cur_price > st.enter && st.ep > maxEp) {
			target_price = std::max(cur_price, st.enter + (st.enter * reduceTarget));
			size = (maxEp - st.ep) / st.enter;
		} else {
			double norm = dist / cfg.target_exit_price_distance;
			double power = std::pow(norm, 4) * cfg.exit_power_mult;
			size = -assets * power;
		}
	}

	// correction for missing assets on exchange
	if (size > 0 && size < st.assets - assets) {
		size = st.assets - assets;
	}

	// explicitly trade only within budget
	if (size > 0) {
		size = std::min(size, availableCurrency / price);
	} else {
		size = std::max(size, -effectiveAssets);
	}

	return {target_price, size, alert};
}

IStrategy::OrderData Strategy_Epa::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {
	logInfo("getNewOrder: new_price=$1, assets=$2, currency=$3, dir=$4", new_price, assets, currency, dir);

	auto res = calculateSize(cur_price, new_price, assets);
	double size = res.size;
	auto alert = res.alert ? IStrategy::Alert::enabled : IStrategy::Alert::disabled;

	logInfo("   -> $1 (alert: $2)", size, res.alert);

	// price where order is put. If this field is 0, recommended price is used
  // size of the order, +buy, -sell. If this field is 0, the order is not placed
	return { res.price, size, alert };
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
	auto alerts = tradeSize == 0 ? (st.alerts + 1) : 0;

	long dir = tradeSize > 0 ? -1 : (tradeSize < 0 ? 1 : (tradePrice > st.last_price ? 1 : -1));
	long sentiment = st.history[0] + st.history[1] + st.history[2] + st.history[3] + st.history[4] + st.history[5] + dir;
	//logInfo("onTrade: tradeSize=$1, assetsLeft=$2, enter=$3, currencyLeft=$4", tradeSize, assetsLeft, enter, currencyLeft);

	return {
		// norm. p, accum, neutral pos, open price
		{ norm_profit, 0, std::isnan(enter) ? tradePrice : enter, 0 },
		PStrategy(new Strategy_Epa(cfg, State { ep, enter, st.budget, newAsset, std::min(st.budget, st.currency - cost), tradePrice, alerts, 
			{ st.history[1], st.history[2], st.history[3], st.history[4], st.history[5], dir }, sentiment }))
	};

}

PStrategy Strategy_Epa::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	auto h = src["history"];
	State st {
			src["ep"].getNumber(),
			src["enter"].getNumber(),
			src["budget"].getNumber(),
			src["assets"].getNumber(),
			src["currency"].getNumber(),
			src["last_price"].getNumber(),
			src["alerts"].getInt(),
			{ h[0].getInt(), h[1].getInt(), h[2].getInt(), h[3].getInt(), h[4].getInt(), h[5].getInt() },
			src["sentiment"].getInt()
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
		{"currency", st.currency},
		{"last_price", st.last_price},
		{"alerts", st.alerts},
		{"history", {st.history[0],st.history[1],st.history[2],st.history[3],st.history[4],st.history[5]}},
		{"sentiment", st.sentiment}
	};
}

std::string_view Strategy_Epa::getID() const {
	return id;
}

double Strategy_Epa::getCenterPrice(double lastPrice, double assets) const {
	// center price for spreads
	
	if (!std::isnan(st.last_price) && st.last_price > 0) {
		lastPrice = st.last_price;
	}

	double cp = cfg.backtest ? 2 * lastPrice : lastPrice;
	logInfo("getCenterPrice: lastPrice=$1, assets=$2 -*> $3", lastPrice, assets, cp);
	return cp;

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
	double size = calculateSize(price, price, st.assets).size;
	
	return ChartPoint{
		true, //true
		st.assets + size, //calcPosition(st.ratio, st.kmult, price),
		st.budget, //calcBudget(st.ratio, st.kmult, price)
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
		{"Currency", st.currency},
		{"Last price", st.last_price},
		{"Alert count", st.alerts},
		{"Market history", {st.history[0],st.history[1],st.history[2],st.history[3],st.history[4],st.history[5]}},
		{"Market sentiment", st.sentiment}
	};
}
