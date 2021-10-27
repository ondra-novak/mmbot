/*
 * spread.cpp
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */




#include "spread.h"
#include "series.h"

#include <cmath>
#include <memory>

class DefaulSpread: public ISpreadFunction {
public:

	class State: public ISpreadState {
	public:
		StreamSMA sma;
		StreamSTDEV stdev;

		State(std::size_t sma_interval,std::size_t stdev_interval);
	};

	DefaulSpread(double sma, double stdev, double force_spread);

	virtual std::unique_ptr<ISpreadState> start() const ;
	virtual Result point(std::unique_ptr<ISpreadState> &state, double y) const;


protected:
	double sma;
	double stdev;
	double force_spread;

};

std::unique_ptr<ISpreadFunction> defaultSpreadFunction(double sma, double stdev, double force_spread) {
	return std::make_unique<DefaulSpread>(sma, stdev, force_spread);


}

VisSpread::VisSpread(const std::unique_ptr<ISpreadFunction> &fn, const Config &cfg)
:fn(fn),state(fn->start()),dynmult(cfg.dynmult),sliding(cfg.sliding),mult(cfg.mult),order2(cfg.order2*0.01)
{


}

VisSpread::Result VisSpread::point(double y) {
	auto sp = fn->point(state, y);
	if (last_price == 0) {
		last_price = y;
		offset = y;
		return {false};
	}
	if (!sp.valid) return {false};

	double spread = sp.spread;
	double center = sliding?sp.center:0;
	double low = (center+offset) * std::exp(-spread*mult*dynmult.getBuyMult());
	double high = (center+offset) * std::exp(spread*mult*dynmult.getSellMult());
	if (sliding && last_price) {
		double low_max = last_price*std::exp(-spread*0.01);
		double high_min = last_price*std::exp(spread*0.01);
		if (low > low_max) {
			high = low_max + (high-low);
			low = low_max;
		}
		if (high < high_min) {
			low = high_min - (high-low);
			high = high_min;

		}
		low = std::min(low_max, low);
		high = std::max(high_min, high);
	}
	double low2 = low * std::exp(-spread*order2);
	double high2 = high * std::exp(spread*order2);
	int trade = 0;
	int trade2 = 0;
	double price = last_price;
	double price2 = 0;
	if (y > high) {
		price = high;
		last_price = high;
		offset = high-center;
		trade = -1;
		if (order2 && y > high2) {
			trade2 =-1;
			price2 = high2;
			offset = high2-center;
			last_price = high2;
		}
		dynmult.update(false,true);
	}
	else if (y < low) {
		price = low;
		last_price = low;
		offset = low-center;
		trade = 1;
		if (order2 && y < low2) {
			last_price = low;
			offset = low-center;
			trade2 = 1;
			price2 = low;
		}
		dynmult.update(true,false);
	}
	else {
		dynmult.update(false,false);
	}
	return {true,price,low,high,trade,price2,trade2};
}

DefaulSpread::DefaulSpread(double sma, double stdev, double force_spread)
	:sma(sma),stdev(stdev),force_spread(force_spread)
{
}

std::unique_ptr<ISpreadState> DefaulSpread::start() const {
	return std::make_unique<State>(std::max<std::size_t>(30,static_cast<std::size_t>(sma*60.0)),
								std::max<std::size_t>(30,static_cast<std::size_t>(stdev*60.0)));
}

DefaulSpread::Result DefaulSpread::point(std::unique_ptr<ISpreadState> &state, double y) const {
	State &st = static_cast<State &>(*state);

	double avg = st.sma << y;
	if (force_spread) {
		return {true, force_spread, avg, 0};
	} else {
		double dv = st.stdev << (y - avg);
		return {true, std::log((avg+dv)/avg), avg, 0};
	}
}

inline DefaulSpread::State::State(std::size_t sma_interval, std::size_t stdev_interval)
	:sma(sma_interval), stdev(stdev_interval)
{

}
