/*
 * istrategy.h
 *
 *  Created on: 17. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTRATEGY_H_
#define SRC_MAIN_ISTRATEGY_H_
#include <imtjson/value.h>
#include "../shared/refcnt.h"

class IStrategy: public ondra_shared::RefCntObj {
public:

	///Strategy is initialized and valid
	/**
	 * @retval true valid and ready
	 * @retval false not ready, you must do initial setup
	 */
	virtual bool isValid() const = 0;


	///Initialized strategy. Because the object is immutable, it creates a new revision and returns it
	/**
	 * @param curPrice current price
	 * @param assets current assets (or position)
	 * @param currency current currencies
	 * @return initialized strategy
	 */
	virtual IStrategy *init(double curPrice, double assets, double currency) const = 0;


	struct OnTradeResult {
		double normProfit;
		double normAccum;
	};

	///Creates a new state after trade
	/**
	 * @param tradePrice price where the trade has been executed
	 * @param tradeSize size of the execution
	 * @param assetsLeft assets left on the account (or position)
	 * @param currencyLeft currency left on the account
	 * @return result of trade and pointer to a new state
	 */
	virtual std::pair<OnTradeResult,IStrategy *> onTrade(double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const = 0;

	///Export state to JSON
	virtual json::Value exportState() const = 0;

	///Import state from JSON
	/** Creates new instance */
	virtual IStrategy *importState(json::Value src) const = 0;


	virtual double calcOrderSize(double price, double assets) const = 0;


	struct MinMax {
		double min;
		double max;
	};

	virtual MinMax calcSafeRange(double assets, double currencies) const = 0;

	virtual ~IStrategy() {}
};



#endif /* SRC_MAIN_ISTRATEGY_H_ */
