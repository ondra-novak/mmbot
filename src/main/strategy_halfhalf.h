/*
 * strategy_halfhalf.h
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HALFHALF_H_
#define SRC_MAIN_STRATEGY_HALFHALF_H_
#include "istrategy.h"

class Strategy_HalfHalf: public IStrategy {
public:
	Strategy_HalfHalf(double ea, double accu, double p = 0, double a = 0);

	virtual bool isValid() const override;
	virtual IStrategy *init(double curPrice, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,IStrategy *> onTrade(double tradePrice, double tradeSize,
						double assetsLeft, double currencyLeft) const override;
	virtual json::Value exportState() const override;
	virtual IStrategy *importState(json::Value src) const override;
	virtual double calcOrderSize(double price, double assets) const override;
	virtual MinMax calcSafeRange(double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual IStrategy *reset() const override;
	virtual std::string_view getID() const override;
	virtual IStrategy *setMarketInfo(const IStockApi::MarketInfo &minfo)  const override;

	static std::string_view id;


protected:
	double ea;
	double accu;
	double p;
	double a;
};

#endif /* SRC_MAIN_STRATEGY_HALFHALF_H_ */
