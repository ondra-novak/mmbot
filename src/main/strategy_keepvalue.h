/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPVALUE_H_
#define SRC_MAIN_STRATEGY_KEEPVALUE_H_
#include "istrategy.h"

class Strategy_KeepValue: public IStrategy {
public:

	struct Config {
		double ea;
		double accum;
	};

	Strategy_KeepValue(const Config &cfg, double p = 0, double a = 0);

	virtual bool isValid() const override;
	virtual IStrategy *init(double curPrice, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,IStrategy *> onTrade(double tradePrice, double tradeSize,
						double assetsLeft, double currencyLeft) const override;
	virtual json::Value exportState() const override;
	virtual IStrategy *importState(json::Value src) const override;
	virtual double calcOrderSize(double price, double assets) const override;
	virtual MinMax calcSafeRange(double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual std::string_view getID() const override;
	virtual IStrategy *reset() const override;

	static std::string_view id;


protected:
	Config cfg;
	double p;
	double a;
};


#endif /* SRC_MAIN_STRATEGY_KEEPVALUE_H_ */
