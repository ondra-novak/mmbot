/*
 * strategy_keepbalance.h
 *
 *  Created on: 8. 11. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPBALANCE_H_
#define SRC_MAIN_STRATEGY_KEEPBALANCE_H_
#include <imtjson/value.h>
#include "istrategy.h"


class Strategy_KeepBalance: public IStrategy {
public:

	struct Config {
		double keep_min;
		double keep_max;
	};

	struct State {
		double last_price = 0;
		double last_balance = 0;
	};

	Strategy_KeepBalance(const Config &cfg);
	Strategy_KeepBalance(const Config &cfg, const State &st);

	virtual bool isValid() const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult, PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;
	virtual json::Value exportState() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual PStrategy importState(json::Value src, const IStockApi::MarketInfo &minfo) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual double calcCurrencyAllocation(double price) const override;
	virtual ChartPoint calcChart(double price) const override;
	virtual bool needLiveBalance() const override {return true;}

	static const std::string_view id;
protected:
	Config cfg;
	State st;
};


#endif /* SRC_MAIN_STRATEGY_KEEPBALANCE_H_ */
