/*
 * default_spread_generator.cpp
 *
 *  Created on: 18. 3. 2022
 *      Author: ondra
 */

#include <imtjson/object.h>

#include "default_spread_generator.h"


#include "sgn.h"


std::string_view ToolName<PSpreadGenerator>::get()
{
	return "spread generator";
}


std::string_view AdaptiveSpreadGenerator::id = "adaptive";
std::string_view FixedSpreadGenerator::id = "fixed";

AdaptiveSpreadGenerator::AdaptiveSpreadGenerator(const Config &cfg)
:cfg(new RefCntCfg(cfg)),state{false}
{
}

double AdaptiveSpreadGenerator::get_order_price(double side, double equilibrium, bool dynmult) const {
	double m = equilibrium;
	if (cfg->sliding) m += state.ema-state.trade_base;
	double s = get_base_spread();
	if (side * state.freeze>0) s = std::min(s, side*state.freeze);
	double dm = dynmult?side>0?state.dynState.getBuyMult():state.dynState.getSellMult():1.0;
	double p = m*std::exp(cfg->mult*s*dm*-side);
	return p;
}

PSpreadGenerator AdaptiveSpreadGenerator::add_point(double price) const {
	if (!state.valid) {
		return new AdaptiveSpreadGenerator(cfg, {
				true,DynMult(),price,0.0,price,0.0
		});
	} else {
		double k = 2.0/(cfg->sma_interval+1);
		double ek = 2.0/(cfg->stdev_interval+1);
		double new_ema = state.ema * (1-k)+price*k;
		double p2 = pow2(new_ema-price);
		double new_ema2 = state.ema2 * (1-k) + p2*ek;
		return new AdaptiveSpreadGenerator(cfg, {
				true, state.dynState.update(*cfg, false, false),new_ema,new_ema2,state.trade_base,state.freeze
		});
	}
}

PSpreadGenerator AdaptiveSpreadGenerator::reset_dynmult() const {
	return new AdaptiveSpreadGenerator(cfg,{
		true, DynMult(), state.ema, state.ema2,state.trade_base,state.freeze
	});
}

double AdaptiveSpreadGenerator::get_base_spread() const {
	return std::log((state.ema+std::sqrt(state.ema2))/state.ema);
}

PSpreadGenerator AdaptiveSpreadGenerator::report_trade(double price, double size) const {
	if (!state.valid) {
		return new AdaptiveSpreadGenerator(cfg, {
				true,DynMult(),price,0.0,price,0.0
		});
	} else if (size) {
		double f = 0;
		if (cfg->freeze) {
			f = get_base_spread() * sgn(size);
		}
		auto dms = state.dynState.update(*cfg, size>0, size<0);
		return new AdaptiveSpreadGenerator(cfg, {
				true, dms, state.ema, state.ema2, state.ema, f
		});
	} else {
		return this;
	}
}

json::Value AdaptiveSpreadGenerator::save() const {
	return json::Object{
		{"valid",state.valid},
		{"buy_mult",state.dynState.getBuyMult()},
		{"sell_mult",state.dynState.getSellMult()},
		{"ema",state.ema},
		{"ema2",state.ema2},
		{"freeze",state.freeze},
		{"trade_base",state.trade_base},
	};
}

PSpreadGenerator AdaptiveSpreadGenerator::load(json::Value value) const {
	return new AdaptiveSpreadGenerator(cfg,
			{
					value["valid"].getBool(),
					DynMult(value["buy_mult"].getNumber(), value["sell_mult"].getNumber()),
					value["ema"].getNumber(),
					value["ema2"].getNumber(),
					value["trade_base"].getNumber(),
					value["freeze"].getNumber(),
			});
}

void AdaptiveSpreadGenerator::reg(SpreadGenRegister &reg) {
	static auto def = json::Value::fromString(R"json(
	[{"name":"sma_interval","type":"slider","min":0,"max":240,"step":0.1,"decimals":1, "default":24},
	 {"name":"stdev_interval","type":"slider","min":0,"max":240,"step":0.1,"decimals":1, "default":8},
	 {"name":"mult","type":"slider","min":-100,"max":100,"step":0.1,"decimals":1, "default":0},
	 {"name":"raise","type":"slider","min":0,"max":1000,"step":0.1,"decimals":1, "default": 400},
	 {"name":"fall","type":"slider","min":0,"max":10,"step":0.1,"decimals":1, "default": 3},
	 {"name":"cap","type":"number","min":0,"max":100,"default": 100},
	 {"name":"mode","type":"enum","options":["disabled","independent","together","alternate","half_alternate"], "default":"independent"},
 	 {"name":"mult_factor","type":"checkbox","default":false},
 	 {"name":"sliding","type":"checkbox","default":false},
	 {"name":"freeze","type":"checkbox","default":false}
	])json");

	reg.reg_tool({
		std::string(id),"Adaptive","default",def
		}) >> [] (json::Value cfg) {
			return new AdaptiveSpreadGenerator(Config{
				cfg["raise"].getNumber(),
				cfg["fall"].getNumber(),
				cfg["cap"].getNumber(),
				strDynmult_mode[cfg["mode"].getString()],
				cfg["mult_factor"].getBool(),
				cfg["sma_interval"].getNumber()*60.0,
				cfg["stdev_interval"].getNumber()*60.0,
				std::pow(2.0,cfg["mult"].getNumber()/100.0),
				cfg["sliding"].getBool(),
				cfg["freeze"].getBool()
			});
		};
}

bool AdaptiveSpreadGenerator::is_valid() const {
	return state.valid;
}

AdaptiveSpreadGenerator::AdaptiveSpreadGenerator(const PConfig &cfg, State &&state):cfg(cfg),state(std::move(state)) {
}

double FixedSpreadGenerator::get_order_price(double side, double equilibrium, bool dynmult) const {
	double m = equilibrium;
	double s = get_base_spread();
	double dm = dynmult?side>0?state.dynState.getBuyMult():state.dynState.getSellMult():1.0;
	double p = m*std::exp(cfg->mult*s*dm);
	return p;

}

PSpreadGenerator FixedSpreadGenerator::add_point(double price) const {
	return this;
}

PSpreadGenerator FixedSpreadGenerator::reset_dynmult() const {
	return new FixedSpreadGenerator(cfg,{});
}

PSpreadGenerator FixedSpreadGenerator::report_trade(double price, double size) const {
	if (size) {
			auto dms = state.dynState.update(*cfg, size>0, size<0);
			return new FixedSpreadGenerator(cfg, {dms});
		} else {
			return this;
		}

}

double FixedSpreadGenerator::get_base_spread() const {
	return cfg->spread_pct;
}


FixedSpreadGenerator::FixedSpreadGenerator(const Config &cfg):cfg(new RefCntCfg(cfg)) {
}

json::Value FixedSpreadGenerator::save() const {
	return json::Object{
		{"buy_mult",state.dynState.getBuyMult()},
		{"sell_mult",state.dynState.getSellMult()},
	};

}

PSpreadGenerator FixedSpreadGenerator::load(json::Value value) const {
	return new FixedSpreadGenerator(cfg,
			{
					DynMult(value["buy_mult"].getNumber(), value["sell_mult"].getNumber()),
			});
}

void FixedSpreadGenerator::reg(SpreadGenRegister &reg) {
	static auto def = json::Value::fromString(R"json(
	[{"name":"spread_pct","type":"slider","min":0.05,"max":20,"step":0.05,"decimals":2, "default":1},
	 {"name":"raise","type":"slider","min":0,"max":1000,"step":0.1,"decimals":1, "default": 400},
	 {"name":"fall","type":"slider","min":0,"max":10,"step":0.1,"decimals":1, "default": 3},
	 {"name":"cap","type":"number","min":0,"max":100,"default": 100},
	 {"name":"mode","type":"enum","options":["disabled","independent","together","alternate","half_alternate"], "default":"independent"},
 	 {"name":"mult_factor","type":"checkbox","default":false},
 	 {"name":"sliding","type":"checkbox","default":false}
	])json");

	reg.reg_tool({std::string(id),"Fixed","default", def}) >> [] (json::Value cfg) {
			return new FixedSpreadGenerator(Config{
				cfg["raise"].getNumber(),
				cfg["fall"].getNumber(),
				cfg["cap"].getNumber(),
				strDynmult_mode[cfg["mode"].getString()],
				cfg["mult_factor"].getBool(),
				cfg["spread_pct"].getNumber(),
			});
	};
}

FixedSpreadGenerator::FixedSpreadGenerator(const PConfig &cfg, State &&state):cfg(cfg),state(std::move(state)) {
}

double AdaptiveSpreadGenerator::get_buy_mult() const {return state.dynState.getBuyMult();}
double AdaptiveSpreadGenerator::get_sell_mult() const {return state.dynState.getSellMult();}
double FixedSpreadGenerator::get_buy_mult() const {return state.dynState.getBuyMult();}
double FixedSpreadGenerator::get_sell_mult() const {return state.dynState.getSellMult();}

