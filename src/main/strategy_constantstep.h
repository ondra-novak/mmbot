/*
 * strategy_constantstep.h
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_CONSTANTSTEP_H_
#define SRC_MAIN_STRATEGY_CONSTANTSTEP_H_

#include "istrategy.h"

class Strategy_ConstantStep: public IStrategy {
public:
	struct Config {
			double ea;
			double accum;
		};

	struct State {
			double a = 0;
			double p = 0;
			double f = 0; //fiat
			double m = 0;
		};


	Strategy_ConstantStep(const Config &cfg, State &&st);
	Strategy_ConstantStep(const Config &cfg);

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
	virtual double calcCurrencyAllocation(double price, bool leveraged) const override;

	PStrategy init(const IStockApi::MarketInfo &m, double price, double assets, double cur) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo & , double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual ChartPoint calcChart(double price) const override;
	virtual double getCenterPrice(double lastPrice, double assets) const override {return getEquilibrium(assets);}



	struct Consts {
		double k;
		double c;
	};

	static Consts calcConsts(double a, double p, double max);
	double calcA(double price) const;
	static double calcA(const Consts  &cst, double price);

	static std::string_view id;
	static double calcAccumulation(const State &st, const Config &cfg, double price);
	static double calcAccountValue(const Consts &cst,  double price);


protected:
	Config cfg;
	State st;

};

#endif /* SRC_MAIN_STRATEGY_CONSTANTSTEP_H_ */
