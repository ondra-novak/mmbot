/*
 * strategy_constantstep.h
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_DCASHITCOIN_H_
#define SRC_MAIN_STRATEGY_DCASHITCOIN_H_

#include "istrategy.h"

class Strategy_DcaShitcoin: public IStrategy {
public:
    
    class IntTable;
    
	struct Config {
	};

	struct State {
        double k = 0;
        double w = 0;
        double p = 0;
	};


	Strategy_DcaShitcoin(const Config &cfg, State &&st);
	Strategy_DcaShitcoin(const Config &cfg);

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

	PStrategy init(bool spot, double price, double assets, double cur) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo & , double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual ChartPoint calcChart(double price) const override;
	virtual double getCenterPrice(double lastPrice, double assets) const override {return getEquilibrium(assets);}


	static double calcPos(double k, double w, double price);
	static double calcBudget(double k, double w, double price);
	static double calcPosInv(double k, double w, double pos);
//	static double calcBudgetInv(double k, double w, double budget);
    static double calcCur(double k, double w, double price);
    static double calcCurInv(double k, double w, double cur);
    static double calcRatio(double k, double cur);
    static double calcRatioInv(double x, double ratio);

	static std::string_view id;

protected:
	Config cfg;
	State st;

};

#endif /* SRC_MAIN_STRATEGY_DCASHITCOIN_H_ */
