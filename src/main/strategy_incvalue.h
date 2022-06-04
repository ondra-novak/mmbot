/*
 * strategy_incvalue.h
 *
 *  Created on: 2. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_INCVALUE_H_
#define SRC_MAIN_STRATEGY_INCVALUE_H_

#include <string_view>
#include <optional>
#include "istrategy.h"

class Strategy_IncValue: public IStrategy {
public:

	struct Function{
		double z = 1;
		double pos(double w, double k, double x) const;
		double budget(double w, double k, double x) const;
		double currency(double w, double k, double x) const;
		double root(double w, double v, double a) const;
	};

	struct Config {
		Function fn;
		double w;	//argument w - increade
		double r;   //argument r - reduction ratio
		double ms;  //max spread
		bool reinvest;
	};

	struct State {
		bool spot = true;
		double k = 0;	//k - neutral price
		double v = 0;	//v - value after last trade
		double p = 0;   //p - last trade price
		double b = 0;
	};



	Strategy_IncValue(const Config &cfg);
	Strategy_IncValue(const Config &cfg, State &&st);
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
	virtual double calcCurrencyAllocation(double price, bool leveraged) const;
	virtual IStrategy::ChartPoint calcChart(double price) const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual PStrategy reset() const;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const;

	static std::string_view id;

	PStrategy init(bool spot, double price, double assets, double currency) const;

protected:
	Config cfg;
	State st;

	double calc_newk(double pnl, double new_price) const;
	double calcW() const;
	static double calcW(double w, double b, double p) ;

};

#endif /* SRC_MAIN_STRATEGY_INCVALUE_H_ */
