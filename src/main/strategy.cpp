/*
 * strategy.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy.h"

#include <cmath>
#include <imtjson/namedEnum.h>
#include <imtjson/object.h>
#include "../shared/stringview.h"
#include "strategy_plfrompos.h"
#include "strategy_halfhalf.h"
#include "strategy_harmonic.h"
#include "strategy_keepvalue.h"
#include "strategy_stairs.h"

static json::NamedEnum<Strategy_PLFromPos::CloseMode> strCloseMode ({
		{Strategy_PLFromPos::always_close,"always_close"},
		{Strategy_PLFromPos::prefer_close,"prefer_close"},
		{Strategy_PLFromPos::prefer_reverse,"prefer_reverse"}
});

static json::NamedEnum<Strategy_PLFromPos::ReduceMode> strReduceMode ({
		{Strategy_PLFromPos::reduceFromProfit,"rp"},
		{Strategy_PLFromPos::reduceFromProfit,""},
		{Strategy_PLFromPos::fixedReduce,"fixed"},
		{Strategy_PLFromPos::neutralMove,"npmove"},
		{Strategy_PLFromPos::toOpenPrice ,"openp"},
		{Strategy_PLFromPos::ema,"ema"},
		{Strategy_PLFromPos::overload,"overload"}
});

static json::NamedEnum<Strategy_Stairs::Pattern> strStairsPattern ({
		{Strategy_Stairs::arithmetic,"arithmetic"},
		{Strategy_Stairs::constant,""},
		{Strategy_Stairs::constant,"constant"},
		{Strategy_Stairs::exponencial,"exponencial"},
		{Strategy_Stairs::harmonic,"harmonic"}
});

using ondra_shared::StrViewA;
Strategy Strategy::create(std::string_view id, json::Value config) {

	if (id== Strategy_PLFromPos::id) {
		Strategy_PLFromPos::Config cfg;
		cfg.accum = config["accum"].getNumber();
		cfg.step = config["cstep"].getNumber();
		cfg.neutral_pos = config["neutral_pos"].getNumber();
		cfg.maxpos = config["maxpos"].getNumber();
		cfg.reduce_factor = config["reduce_factor"].getNumber();
		cfg.power= config["power"].getNumber();
		if (config["fixed_reduce"].getBool() || cfg.reduce_factor < 0) {
			cfg.reduceMode = Strategy_PLFromPos::fixedReduce;
		} else {
			cfg.reduceMode = strReduceMode[config["reduce_mode"].getString()];
		}
		cfg.reduce_factor = std::abs(cfg.reduce_factor);
		cfg.baltouse= config["balance_use"].defined()?config["balance_use"].getNumber():1;
		cfg.stoploss_reverse=config["slreverse"].getNumber();
		return Strategy(new Strategy_PLFromPos(cfg,{}));
	} else if (id == Strategy_HalfHalf::id) {
		Strategy_HalfHalf::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_HalfHalf(cfg));
	} else if (id == Strategy_KeepValue::id) {
		Strategy_KeepValue::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.chngtm = config["valinc"].getNumber();
		return Strategy(new Strategy_KeepValue(cfg,{}));
	} else if (id == Strategy_Harmonic::id) {
		Strategy_Harmonic::Config cfg;
		cfg.power = config["power"].getNumber();
		cfg.close_first= config["close_first"].getBool();
		cfg.favor_trend= config["favor_trend"].getNumber();
		return Strategy(new Strategy_Harmonic(cfg));
	} else if (id == Strategy_Stairs::id) {
		Strategy_Stairs::Config cfg;
		cfg.power = config["power"].getNumber();
		cfg.neutral_pos= config["neutral_pos"].getNumber();
		cfg.zero_step= config["zero_step"].getBool();
		cfg.max_steps=config["max_steps"].getInt();
		cfg.close_on_reverse=config["close_on_reverse"].getBool();
		cfg.pattern=strStairsPattern[config["pattern"].getString()];
		return Strategy(new Strategy_Stairs(cfg));
	} else {
		throw std::runtime_error(std::string("Unknown strategy: ").append(id));
	}

}

json::Value Strategy::exportState() const {
	return json::Object(ptr->getID(), ptr->exportState());
}

void Strategy::importState(json::Value src) {
	json::Value data = src[ptr->getID()];
	ptr = ptr->importState(data);
}

double IStrategy::calcOrderSize(double expectedAmount, double actualAmount, double newAmount) {
	double middle = (actualAmount + expectedAmount)/2;
	double size = newAmount - middle;
	return size;
}

void Strategy::setConfig(const ondra_shared::IniConfig::Section &cfg) {
	Strategy_PLFromPos::sliding_zero_factor = cfg["sliding_zero_reverse"].getNumber(0.9);
	Strategy_PLFromPos::min_rp_reduce = cfg["min_rp_reduce"].getNumber(0.1);
}

void Strategy::adjustOrder(double dir, double mult,
		bool enable_alerts, Strategy::OrderData &order) {

	if (order.size * dir < 0) {
		order.alert = order.alert || enable_alerts;
		order.size = 0;
	} else if (order.size * dir > 0) {
		order.alert = false;
	}

	order.size *= mult;

}
