/*
 * strategy_plfrompos.h
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_PLFROMPOS_H_
#define SRC_MAIN_STRATEGY_PLFROMPOS_H_

#include "istrategy.h"

class Strategy_PLFromPos: public IStrategy {
public:
	struct Config {
		double step;
		double accum;
		double neutral_pos;
	};

	Strategy_PLFromPos(const Config &cfg, double p = 0, double a = 0, double pos = 0, double err = 0);

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
	double pos;
	double err;
};

#endif /* SRC_MAIN_STRATEGY_PLFROMPOS_H_ */
