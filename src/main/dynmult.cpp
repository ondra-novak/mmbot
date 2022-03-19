/*
 * dynmult.cpp
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#include "dynmult.h"

void DynMultControl::setMult(double buy, double sell) {
	state = DynMult(buy,sell);
}

double DynMultControl::getBuyMult() const {
	return state.getBuyMult();
}

double DynMultControl::getSellMult() const {
	return state.getSellMult();
}

double DynMultControl::raise_fall(double v, bool israise) const {
	return DynMult::raise_fall(cfg, v, israise);
}

void DynMultControl::update(bool buy_trade, bool sell_trade) {

	state = state.update(cfg, buy_trade, sell_trade);
}

void DynMultControl::reset() {
	state = DynMult();
}

double DynMult::raise_fall(const Config &cfg, double v, bool israise) {
	if (israise) {
		double rr = cfg.raise/100.0;
		return std::min(cfg.mult?v*(1+rr):v + rr, cfg.cap);
	} else {
		double ff = cfg.fall/100.0;
		return std::max(1.0,cfg.mult?v*(1.0-ff):v - ff);
	}

}
DynMult DynMult::update(const Config &cfg, bool buy_trade,bool sell_trade) const {
	double b = mult_buy;
	double s = mult_sell;
	switch (cfg.mode) {
	case Dynmult_mode::disabled:
		return DynMult();
	default:
	case Dynmult_mode::independent:
		break;
	case Dynmult_mode::together:
		buy_trade = buy_trade || sell_trade;
		sell_trade = buy_trade;
		break;
	case Dynmult_mode::alternate:
		if (buy_trade) s = 0;
		else if (sell_trade) s = 0;
		break;
	case Dynmult_mode::half_alternate:
		if (buy_trade) s = ((s-1) * 0.5) + 1;
		else if (sell_trade) b = ((b-1) * 0.5) + 1;
		break;
	}
	return DynMult(raise_fall(cfg, b, buy_trade),raise_fall(cfg, b, sell_trade));

}

