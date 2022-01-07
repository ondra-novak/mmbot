/*
 * strategy_pile.h
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HODLSHORT_H_
#define SRC_MAIN_STRATEGY_HODLSHORT_H_

#include <string_view>

#include "istrategy.h"

class Strategy_Hodl_Short: public IStrategy {
public:




	struct Config {
		double z; 	//parameter exponent in formula
		double b;
		double acc;
		bool reinvest;
	};

	struct State {
		double w = 0;		//parameter w - initialized during init
		double k = 0;       //parameter k - neutral price
		double lastp = 0; // last price
		double a = 0;
		double val = 0;
		double accm = 0;
	};

	static double calcAssets(double k, double w, double z, double x);
	static double calcBudget(double k, double w, double z, double x);
	static double calcFiat(double k, double w, double z, double x);
	static double calcPriceFromAssets(double k, double w, double z, double a);
	static double calcKFromAssets(double w, double z, double a, double x);




	Strategy_Hodl_Short(const Config &cfg);
	Strategy_Hodl_Short(const Config &cfg, State &&st);

	PStrategy init(double price, double assets, double currency, bool leveraged) const;

	static std::string_view id;

	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const;
	virtual bool isValid() const;
	virtual json::Value exportState() const;
	virtual std::string_view getID() const;
	virtual double getCenterPrice(double lastPrice, double assets) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const;
	virtual IStrategy::BudgetInfo getBudgetInfo() const;
	virtual double getEquilibrium(double assets) const;
	virtual double calcCurrencyAllocation(double price) const;
	virtual IStrategy::ChartPoint calcChart(double price) const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual PStrategy reset() const;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const;




protected:
	Config cfg;
	State st;

	double calcNewK(double new_price, double step) const;

};

#endif /* SRC_MAIN_STRATEGY_HODLSHORT_H_ */
