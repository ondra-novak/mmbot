/*
 * strategy_martingale.h
 *
 *  Created on: 16. 3. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_MARTINGALE_H_
#define SRC_MAIN_STRATEGY_MARTINGALE_H_

#include <chrono>

#include "istrategy.h"

class Strategy_Martingale: public IStrategy {
public:

	struct Config {
		double initial_step;  //initial step in % of balance
		double power;         //power between 0 and 1, can be higher but not lower
		double reduction;	  //reduction between 0 and 1.
		double collateral;
		bool allow_short;		//allow shorts
	};


	struct State {
		double pos = 0;			//current position
		double price = 0;		//last price - where position was changed
		double enter_price = 0;	//enter price
		double exit_price = 0;	//exit price
		double value = 0;		//possible profit, made by closing position exactly at exit price
		double budget = 0;		//budget calculation - recalculated on position reset
		double initial = 0;     //initial volumes
	};


	Strategy_Martingale(const Config &cfg);
	Strategy_Martingale(const Config &cfg, State &&state);

	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const override;
	virtual std::pair<IStrategy::OnTradeResult, 	PStrategy> onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const  override;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const  override;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const  override;
	virtual bool isValid() const  override;
	virtual json::Value exportState() const  override;
	virtual std::string_view getID() const  override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const  override;
	virtual IStrategy::BudgetInfo getBudgetInfo() const  override;
	virtual double getEquilibrium(double assets) const  override;
	virtual double calcCurrencyAllocation(double price) const  override;
	virtual IStrategy::ChartPoint calcChart(double price) const  override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const  override;
	virtual PStrategy reset() const  override;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const  override;
	virtual bool needLiveBalance() const  override;

	static std::string_view id;


	PStrategy init(double pos, double price, double currency) const;
	double calcPos(double new_price) const;
protected:

	Config cfg;
	State st;
};


#endif /* SRC_MAIN_STRATEGY_MARTINGALE_H_ */
