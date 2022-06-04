/*
 * invert_strategy.h
 *
 *  Created on: 13. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_INVERT_STRATEGY_H_
#define SRC_MAIN_INVERT_STRATEGY_H_

#include <optional>
#include "istrategy.h"


class InvertStrategy: public IStrategy {
public:

	InvertStrategy(PStrategy target);
	InvertStrategy(PStrategy target,double collateral, double last_price, double last_pos);
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

protected:

	PStrategy target;
	double collateral=1;
	double last_price=1;
	double last_pos = 0;
	bool inited = false;

	IStockApi::MarketInfo invert(const IStockApi::MarketInfo &src) const;
	static IStockApi::MarketInfo invert(const IStockApi::MarketInfo &src, double cur_price) ;
	double getPosition(double assets) const;
	static double getPosition(double assets, double collateral, double price);
	static double getBalance(double currency, double price) ;
	double getAssetsFromPos(double pos) const;
	static double getAssetsFromPos(double pos, double collateral, double price);
	static double getCurrencyFromBal(double bal, double price) ;


};



#endif /* SRC_MAIN_INVERT_STRATEGY_H_ */
