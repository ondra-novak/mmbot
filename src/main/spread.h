/*
 * spread.h
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SPREAD_H_
#define SRC_MAIN_SPREAD_H_

#include <memory>
#include <optional>
#include <variant>
#include <imtjson/value.h>
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
	virtual std::size_t time_range() const = 0;
	virtual ~ISpreadFunction() {}
};


struct AdaptiveSpreadConfig {
    double sma_range;
    double stdev;
};

struct FixedSpreadConfig {
    double sma;
    double spread;
};

struct RangeSpreadConfig {
    double range;
};

using SpreadConfig = std::variant<AdaptiveSpreadConfig, FixedSpreadConfig, RangeSpreadConfig>;

std::unique_ptr<ISpreadFunction> defaultSpreadFunction(const SpreadConfig &cfg);


class VisSpread {
public:
	struct Config {
		DynMultControl::Config dynmult;
		double mult = 1;
		double order2 = 0;
		bool sliding = false;
		bool freeze = false;
	};

	struct Result {
		bool valid = false;
		double price, low, high;
		int trade; //0=no trade, -1=sell, 1=buy
		double price2 = 0; //price of secondary trade
		int trade2 = 0;  //0 = no secondary trade, -1=sell, 1=buy
	};

	VisSpread(const std::unique_ptr<ISpreadFunction> &fn, const Config &cfg);
	Result point(double y);

protected:
	const std::unique_ptr<ISpreadFunction> &fn;
	std::unique_ptr<ISpreadState> state;
	DynMultControl dynmult;
	bool sliding;
	bool freeze;
	double mult;
	double order2;
	double offset = 0;
	double last_price = 0;
	std::optional<double> chigh, clow;
	double cspread;
	int frozen_side = 0;
	double frozen_spread = 0;
};

///Parse spread config
/**
 * @param v config in json
 * @param mtrader valid for compatibility only (read old config) - true if
 * config is read from mtrader, false if read from strategy testing
 * @return
 */
SpreadConfig parseSpreadConfig(const json::Value &v, bool mtrader);


#endif /* SRC_MAIN_SPREAD_H_ */
