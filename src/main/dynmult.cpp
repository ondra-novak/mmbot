/*
 * dynmult.cpp
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#include "dynmult.h"

void DynMultControl::setMult(double buy, double sell) {
	this->mult_buy = buy;
	this->mult_sell = sell;
}

double DynMultControl::getBuyMult() const {
	return mult_buy;
}

double DynMultControl::getSellMult() const {
	return mult_sell;
}

double DynMultControl::raise_fall(double v, bool israise) {
	if (israise) {
		double rr = cfg.raise/100.0;
		return std::min(cfg.mult?v*(1+rr):v + rr, cfg.cap);
	} else {
		double ff = cfg.fall/100.0;
		return std::max(1.0,cfg.mult?v*(1.0-ff):v - ff);
	}

}

void DynMultControl::update(bool buy_trade, bool sell_trade) {

	switch (cfg.mode) {
	case Dynmult_mode::disabled:
		mult_buy = 1.0;
		mult_sell = 1.0;
		return;
	case Dynmult_mode::independent:
		break;
	case Dynmult_mode::together:
		buy_trade = buy_trade || sell_trade;
		sell_trade = buy_trade;
		break;
	case Dynmult_mode::alternate:
		if (buy_trade) mult_sell = 0;
		else if (sell_trade) mult_buy = 0;
		break;
	case Dynmult_mode::half_alternate:
		if (buy_trade) mult_sell = ((mult_sell-1) * 0.5) + 1;
		else if (sell_trade) mult_buy = ((mult_buy-1) * 0.5) + 1;
		break;
	}
	mult_buy= raise_fall(mult_buy, buy_trade);
	mult_sell= raise_fall(mult_sell, sell_trade);
}

void DynMultControl::reset() {
	mult_buy = 1.0;
	mult_sell = 1.0;
}


