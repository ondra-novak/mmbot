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
		StreamBest<double, std::greater<double> > maxSpread;

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
:fn(fn),state(fn->start()),dynmult(cfg.dynmult),sliding(cfg.sliding),freeze(cfg.freeze),mult(cfg.mult),order2(cfg.order2*0.01)
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

	int trade = 0;
	int trade2 = 0;
	double price = last_price;
	double price2 = 0;

	double center = sliding?sp.center:0;
	if (chigh.has_value() && y>*chigh) {
		double high2 = *chigh * std::exp(cspread*order2);
		price = *chigh;
		last_price = *chigh;
		offset = *chigh-center;
		trade = -1;
		if (order2 && y > high2) {
			trade2 =-1;
			price2 = high2;
			offset = high2-center;
			last_price = high2;
		}
		dynmult.update(false,true);
		/*if (frozen_side != -1)*/ {
			frozen_side = -1;
			frozen_spread = cspread;
		}
	}
	else if (clow.has_value() && y < *clow) {
		double low2 = *clow * std::exp(-cspread*order2);
		price = *clow;
		last_price = *clow;
		offset = *clow-center;
		trade = 1;
		if (order2 && y < low2) {
			last_price = low2;
			trade2 = 1;
			offset = low2-center;
			price2 = low2;
		}
		dynmult.update(true,false);
		/*if (frozen_side != 1)*/ {
			frozen_side = 1;
			frozen_spread = cspread;
		}
	}
	dynmult.update(false,false);

	double lspread = sp.spread;
	double hspread = sp.spread;
	if (freeze) {
		if (frozen_side<0) {
			lspread = std::min(frozen_spread, lspread);
		} else if (frozen_side>0) {
			hspread = std::min(frozen_spread, hspread);
		}
	}
	double low = (center+offset) * std::exp(-lspread*mult*dynmult.getBuyMult());
	double high = (center+offset) * std::exp(hspread*mult*dynmult.getSellMult());
	if (sliding && last_price) {
		double low_max = last_price*std::exp(-lspread*0.01);
		double high_min = last_price*std::exp(hspread*0.01);
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
	low = std::min(low,y);
	high = std::max(high,y);
	chigh = high;
	clow = low;
	cspread = sp.spread;
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
	    double rf = 0.01/std::sqrt(st.sma.size());
		double dv = st.stdev << (y - avg);
		return {true,  std::max(rf,std::log((avg+dv)/avg)), avg, 0};
	}
}

inline DefaulSpread::State::State(std::size_t sma_interval, std::size_t stdev_interval)
	:sma(sma_interval), stdev(stdev_interval),maxSpread(10)
{

}
