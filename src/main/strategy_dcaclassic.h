/*
 * strategy_constantstep.h
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_DCACLASSIC_H_
#define SRC_MAIN_STRATEGY_DCACLASSIC_H_

#include "istrategy.h"

enum class DCAFunction {
    //linear amount
    lin_amount,
    //linear volume
    lin_volume,
    //linear value
    lin_value,
    //margingale
    martingale,
};



template<DCAFunction fn>
struct Strategy_DCA_Config {    
};

template<>
struct Strategy_DCA_Config<DCAFunction::lin_value> {
    double max_drop;
};

template<>
struct Strategy_DCA_Config<DCAFunction::martingale> {
    double initial_step;
    double exponent;
    double cutoff;
};



template<DCAFunction fn>
class Strategy_DCA: public IStrategy {
public:
	using Config = Strategy_DCA_Config<fn>;
	
	struct State {
	        double k = 0;
	        double w = 0;
	        double p = 0;
	        double hlp = 0;
		};


	Strategy_DCA(const Config &cfg, State &&st);
	Strategy_DCA(const Config &cfg);

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
	virtual double getCenterPrice(double lastPrice, double assets) const override;


	static double calcPos(const Config &cfg, double k, double w, double price);
	static double calcBudget(const Config &cfg, double k, double w, double price);
	static double calcPosInv(const Config &cfg, double k, double w, double pos);
//	static double calcBudgetInv(double k, double w, double budget);
    static double calcCur(const Config &cfg, double k, double w, double price);
    static double calcCurInv(const Config &cfg, double k, double w, double cur);
    static double findKFromRatio(const Config &cfg, double price, double ratio);

	static std::string_view id;
		
	void adjust_state(State &nst, double tradePrice, double tradeSize, double prevPos) const;

protected:
	Config cfg;
	State st;

};



using Strategy_DCAClassic = Strategy_DCA<DCAFunction::lin_amount>;
using Strategy_DCAVolume = Strategy_DCA<DCAFunction::lin_volume>;
using Strategy_DCAValue = Strategy_DCA<DCAFunction::lin_value>;
using Strategy_DCAMartngale = Strategy_DCA<DCAFunction::martingale>;

#endif /* SRC_MAIN_STRATEGY_DCACLASSIC_H_ */
