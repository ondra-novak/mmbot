/*
 * strategy_keepbalance.cpp
 *
 *  Created on: 8. 11. 2020
 *      Author: ondra
 */

#include "strategy_keepbalance.h"
#include <stdexcept>
#include "../imtjson/src/imtjson/object.h"

const std::string_view Strategy_KeepBalance::id = "keep_balance";

Strategy_KeepBalance::Strategy_KeepBalance(const Config &cfg):cfg(cfg) {
}

Strategy_KeepBalance::Strategy_KeepBalance(const Config &cfg, const State &st):cfg(cfg),st(st) {
}


bool Strategy_KeepBalance::isValid() const {
	return st.last_price > 0;
}

PStrategy Strategy_KeepBalance::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (cfg.keep_min > cfg.keep_max) throw std::runtime_error("keep_min > keep_max");

	return new Strategy_KeepBalance(cfg, {
			st.last_price?st.last_price:curTicker.last, currency
	});
}

std::pair<Strategy_KeepBalance::OnTradeResult, PStrategy> Strategy_KeepBalance::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {


	return {
		OnTradeResult{0,0},
		new Strategy_KeepBalance(cfg, {
				tradePrice, currencyLeft
		})
	};

}

json::Value Strategy_KeepBalance::exportState() const {
	return json::Value();
}

PStrategy Strategy_KeepBalance::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	return reset();
}

Strategy_KeepBalance::OrderData Strategy_KeepBalance::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {


	double diff = currency<cfg.keep_min?cfg.keep_min - currency:currency>cfg.keep_max?cfg.keep_max- currency:0;
	double asst = -diff / cur_price;
	if (asst * dir <= 0) return {0, 0, IStrategy::Alert::disabled};
	else return {cur_price, asst, IStrategy::Alert::disabled};

}

Strategy_KeepBalance::MinMax Strategy_KeepBalance::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {
	return MinMax{0,std::numeric_limits<double>::infinity()};
}

double Strategy_KeepBalance::getEquilibrium(double assets) const {
	return st.last_price;
}

PStrategy Strategy_KeepBalance::reset() const {
	return new Strategy_KeepBalance(cfg);
}

std::string_view Strategy_KeepBalance::getID() const {
	return id;
}

double Strategy_KeepBalance::calcInitialPosition(
		const IStockApi::MarketInfo &minfo, double price, double assets,
		double currency) const {
	return assets;
}

Strategy_KeepBalance::BudgetInfo Strategy_KeepBalance::getBudgetInfo() const {
	return {
		st.last_balance, 0
	};
}

double Strategy_KeepBalance::calcCurrencyAllocation(double price, bool leveraged) const {
	return cfg.keep_min;
}

json::Value Strategy_KeepBalance::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object({
		{"Last price", minfo.invert_price?1.0/st.last_price:st.last_price},
		{"Last balance", st.last_balance}
	});
}

Strategy_KeepBalance::ChartPoint Strategy_KeepBalance::calcChart(double price) const {
	return {false};
}
