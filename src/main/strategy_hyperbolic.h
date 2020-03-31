/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#define SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#include <chrono>

#include "istrategy.h"

class Strategy_Hyperbolic: public IStrategy {
public:

	struct Config {
		double power;
		double asym;
		double max_loss;
		double reduction;
	};


	struct State {
		double neutral_price = 0;
		double last_price =0;
		double position = 0;
		double mult = 0;
		double val = 0;
	};

	Strategy_Hyperbolic(const Config &cfg, State &&st);
	Strategy_Hyperbolic(const Config &cfg);

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
	mutable std::optional<MinMax> rootsCache;

	struct PosCalcRes {
		bool limited;
		double pos;
	};

	static Strategy_Hyperbolic init(const Config &cfg, double price, double pos, double currency);
	PosCalcRes calcPosition(double price) const;
	static double calcPosValue(double power, double asym, double neutral, double curPrice);
	static MinMax calcRoots(double power, double asym, double neutral, double balance);

	MinMax calcRoots() const;
	double adjNeutral(double price, double value) const;
	static double calcNeutral(double power, double asym, double position, double curPrice);

private:
	double calcMaxLoss() const;
};


#endif /* SRC_MAIN_STRATEGY_HYPERBOLIC_H_ */
