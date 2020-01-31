/*
 * strategy_harmonic.h
 *
 *  Created on: 29. 1. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HARMONIC_H_
#define SRC_MAIN_STRATEGY_HARMONIC_H_

#include "istrategy.h"

class Strategy_Harmonic: public IStrategy {
public:
	struct Config {
		double power;
		bool close_first;
		double favor_trend;
	};

	struct State {
		int streak = 0;
		int inbalance = 0;
		double p = 0;
		double open_price = 0;
		double cur = 0;
	};

	Strategy_Harmonic(const Config &cfg, const State &state = State{0,0,0,0,0});
	virtual ~Strategy_Harmonic();
	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency) const;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const;
	virtual bool isValid() const;
	virtual json::Value exportState() const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual double getEquilibrium() const;
	virtual PStrategy reset() const;
	virtual std::string_view getID() const;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const;
	virtual PStrategy importState(json::Value src) const;

	static std::string_view id;


	double calcPower(double currency) const;

protected:
	Config cfg;
	State st;
};

#endif /* SRC_MAIN_STRATEGY_HARMONIC_H_ */
