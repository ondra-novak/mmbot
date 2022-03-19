/*
 * default_spread_generator.cpp
 *
 *  Created on: 18. 3. 2022
 *      Author: ondra
 */


#include "default_spread_generator.h"
#include "sgn.h"

DefaulSpreadGenerator::DefaulSpreadGenerator(const Config &cfg)
:cfg(new RefCntCfg(cfg)),state{false}
{
}

double DefaulSpreadGenerator::get_order_price(double side, double equilibrium, bool dynmult) const {
	double m = equilibrium;
	if (cfg->sliding) m += state.ema-state.trade_base;
	double s = get_base_spread();
	if (side * state.freeze>0) s = std::min(s, side*state.freeze);
	double dm = dynmult?side>0?state.dynState.getBuyMult():state.dynState.getSellMult():1.0;
	double p = m*std::exp(cfg->mult*s*dm);
	return p;
}

PSpreadGenerator DefaulSpreadGenerator::add_point(double price) const {
	if (!state.valid) {
		return new DefaulSpreadGenerator(cfg, {
				true,DynMult(),price,0.0,price,0.0
		});
	} else {
		double k = 2.0/(cfg->sma_interval+1);
		double ek = 2.0/(cfg->stdev_interval+1);
		double new_ema = state.ema * (1-k)+price*k;
		double p2 = pow2(new_ema-price);
		double new_ema2 = state.ema2 * (1-k) + p2*ek;
		return new DefaulSpreadGenerator(cfg, {
				true, state.dynState.update(*cfg, false, false),new_ema,new_ema2,state.trade_base,state.freeze
		});
	}
}

PSpreadGenerator DefaulSpreadGenerator::reset_dynmult() const {
	return new DefaulSpreadGenerator(cfg,{
		true, DynMult(), state.ema, state.ema2,state.trade_base,state.freeze
	});
}

double DefaulSpreadGenerator::get_base_spread() const {
	return std::log((state.ema+state.ema2)/state.ema);
}

PSpreadGenerator DefaulSpreadGenerator::report_trade(double price, double size) const {
	if (!state.valid) {
		return new DefaulSpreadGenerator(cfg, {
				true,DynMult(),price,0.0,price,0.0
		});
	} else if (size) {
		double f = 0;
		if (cfg->freeze) {
			f = get_base_spread() * sgn(size);
		}
		auto dms = state.dynState.update(*cfg, size>0, size<0);
		return new DefaulSpreadGenerator(cfg, {
				true, dms, state.ema, state.ema2, state.ema, f
		});
	}
}

DefaulSpreadGenerator::DefaulSpreadGenerator(const PConfig &cfg,
		State &&state) {
}
