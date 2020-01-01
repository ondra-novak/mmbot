/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPVALUE_H_
#define SRC_MAIN_STRATEGY_KEEPVALUE_H_
#include <chrono>

#include "istrategy.h"

class Strategy_KeepValue: public IStrategy {
public:

	struct Config {
		double ea;
		double accum;
		double chngtm;
	};


	struct State {
			bool valid = false;
			double p = 0;
			double a = 0;
			std::chrono::system_clock::time_point lt;
		};

	Strategy_KeepValue(const Config &cfg, State &&st);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;

	static std::string_view id;

protected:
	Config cfg;
	State st;

	double calcK() const;
};


#endif /* SRC_MAIN_STRATEGY_KEEPVALUE_H_ */
