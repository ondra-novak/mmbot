/*
 * spread.h
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SPREAD_H_
#define SRC_MAIN_SPREAD_H_

#include <memory>
#include "dynmult.h"

class ISpreadState {
public:

	virtual ~ISpreadState() {}

};

class ISpreadFunction {
public:

	struct Result {
		bool valid = false;
		double spread = 0;
		double center = 0;
		int trend = 0;
	};

	virtual std::unique_ptr<ISpreadState> start() const = 0;
	virtual Result point(std::unique_ptr<ISpreadState> &state, double y) const = 0;
};


std::unique_ptr<ISpreadFunction> defaultSpreadFunction(
		double sma,
		double stdev,
		double force_spread);

class VisSpread {
public:
	struct Config {
		DynMultControl::Config dynmult;
		double mult;
		bool sliding;
	};

	struct Result {
		bool valid = false;
		double price, low, high;
		int trade; //0=no trade, -1=sell, 1=buy
	};

	VisSpread(const std::unique_ptr<ISpreadFunction> &fn, const Config &cfg);
	Result point(double y);

protected:
	const std::unique_ptr<ISpreadFunction> &fn;
	std::unique_ptr<ISpreadState> state;
	DynMultControl dynmult;
	bool sliding;
	double mult;
	double offset = 0;
	double last_price = 0;
};



#endif /* SRC_MAIN_SPREAD_H_ */
