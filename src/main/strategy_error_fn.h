/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_ERRORFN_H_
#define SRC_MAIN_STRATEGY_ERRORFN_H_
#include <chrono>

#include "istrategy.h"

class Strategy_ErrorFn: public IStrategy {
public:

	struct Rebalance {
		double hi_p;
		double hi_a;
		double lo_p;
		double lo_a;

	};

	struct Config {
		double ea;
		double accum;
		Rebalance rebalance;
	};


	struct State {
			double a = 0;
			double p = 0;
			double f = 0; //fiat
			double k = 0;
		};

	Strategy_ErrorFn(const Config &cfg, State &&st);
	Strategy_ErrorFn(const Config &cfg);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src, const IStockApi::MarketInfo &minfo) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency, bool rej) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual double calcCurrencyAllocation(double price) const override;
	virtual ChartPoint calcChart(double price) const override;
	virtual double getCenterPrice(double lastPrice, double assets) const override {return getEquilibrium(assets);}



	static std::string_view id;


	static double calcW(double a, double k, double p);
	static double calcA(double w, double k, double p);
	double calcA(double price) const;
	static double calcAccountValue(const State &st,double ea, double price);
	static double calcReqCurrency(const State &st,double ea, double price, const Rebalance &rebalance);
	PStrategy init(const IStockApi::MarketInfo &m, double price, double assets, double cur) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo & , double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;



	static double calcAccumulation(const State &st, const Config &cfg, double price);
	static double calcReqCurrency(double w, double k, double price, double new_price, const Rebalance &rebalance);
	static double calcReqCurrency(double w, double k, double price);
	static double calcAccountValue(double w, double k, double p);
	static double calcEquilibrium(double w, double k, double c) ;


	struct Consts {
		double w;
		double k;
	};

	static Consts calcRebalance(double w, double k, double p, double np, const Rebalance &rebalance);

	State calcNewState(double new_a, double new_p, double new_f, double accum) const;

protected:
	Config cfg;
	State st;



	double calcNormalizedProfit(double tradePrice, double tradeSize) const;
	static double findRoot(double w, double k, double p, double c);
};


#endif /* SRC_MAIN_STRATEGY_ERRORFN_H_ */
