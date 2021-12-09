/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPVALUE2_H_
#define SRC_MAIN_STRATEGY_KEEPVALUE2_H_
#include <chrono>

#include "istrategy.h"

class Strategy_KeepValue2: public IStrategy {
public:

	struct Config {
		double ratio;
		double accum;
		double chngtm;
		bool rebalance;
		bool reinvest;
	};


	struct State {
		double base_pos = 0;
		double kmult = 0;
		double lastp = 0;
		double curr = 0;
		double budget = 0;
		double pos = 0;
		double berror = 0;
		std::uint64_t last_trade_time;
		std::uint64_t last_check_time;
	};

	Strategy_KeepValue2(const Config &cfg);
	Strategy_KeepValue2(const Config &cfg, State &&st);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src,const IStockApi::MarketInfo &minfo) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency, bool rej) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo & , double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual double calcCurrencyAllocation(double price) const override;
	virtual ChartPoint calcChart(double price) const override;
	virtual double getCenterPrice(double lastPrice, double assets) const override {return getEquilibrium(assets);}

	static double calcPosition(double ratio, double kmult, double price, double init_pos);
	static double calcBudget(double ratio, double kmult, double price, double init_pos);
	static double calcEquilibrium(double ratio, double kmul, double position, double init_pos);
	static double calcPriceFromBudget(double ratio, double kmul, double budget, double init_pos);
	static double calcCurrency(double ratio, double kmult, double price, double init_pos);
	static double calcPriceFromCurrency(double ratio, double kmult, double currency, double init_pos);



	static std::string_view id;

protected:
	Config cfg;
	State st;


	double calculateNewNeutral(double a, double price) const;

	struct AccumInfo {
		double normp;
		double norma;
		double newk;
		double newinit;
	};

	AccumInfo calcAccum(double new_price) const;
	double calcInitP() const;
	double calcCurr() const;


};


#endif /* SRC_MAIN_STRATEGY_KEEPVALUE2_H_ */
