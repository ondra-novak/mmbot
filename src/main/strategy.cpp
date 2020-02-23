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
		{Strategy_Stairs::harmonic,"harmonic"},
		{Strategy_Stairs::parabolic,"parabolic"},
		{Strategy_Stairs::sqrt,"sqrt"},
		{Strategy_Stairs::poisson1,"poisson1"},
		{Strategy_Stairs::poisson2,"poisson2"},
		{Strategy_Stairs::poisson3,"poisson3"},
		{Strategy_Stairs::poisson4,"poisson4"},
		{Strategy_Stairs::poisson5,"poisson5"}
});

static json::NamedEnum<Strategy_Stairs::ReductionMode> strStairsRedMode ({
	{Strategy_Stairs::stepsBack,""},
	{Strategy_Stairs::stepsBack,"stepsBack"},
	{Strategy_Stairs::reverse,"reverse"},
	{Strategy_Stairs::lockOnReduce,"lockOnReduce"},
	{Strategy_Stairs::lockOnReverse,"lockOnReverse"},
});

static json::NamedEnum<Strategy_Stairs::TradingMode> strStairsTMode ({
	{Strategy_Stairs::autodetect,""},
	{Strategy_Stairs::autodetect,"auto"},
	{Strategy_Stairs::exchange,"exchange"},
	{Strategy_Stairs::margin,"margin"}
});

using ondra_shared::StrViewA;
Strategy Strategy::create(std::string_view id, json::Value config) {

	if (id== Strategy_PLFromPos::id) {
		Strategy_PLFromPos::Config cfg;
		cfg.step = config["cstep"].getNumber();
		cfg.pos_offset = config["pos_offset"].getNumber();
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
		cfg.reduce_on_increase=config["reduce_on_inc"].getBool();
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
	} else if (id == Strategy_Stairs::id) {
		Strategy_Stairs::Config cfg;
		cfg.power = config["power"].getNumber();
		cfg.reduction= config["reduction_steps"].getValueOrDefault(2);
		cfg.max_steps=config["max_steps"].getInt();
		cfg.pattern=strStairsPattern[config["pattern"].getString()];
		cfg.mode = strStairsTMode[config["mode"].getString()];
		cfg.redmode = strStairsRedMode[config["redmode"].getString()];
		cfg.sl = config["sl"].getBool();
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

	if (order.alert != IStrategy::Alert::stoploss) {
		order.size *= mult;
	} else {
		order.alert = IStrategy::Alert::forced;
	}
	if (order.size * dir <= 0) {
		order.alert = order.alert == IStrategy::Alert::forced || (enable_alerts && order.alert == IStrategy::Alert::enabled)?IStrategy::Alert::forced:IStrategy::Alert::disabled;
		order.size = 0;
	} else {
		order.alert = IStrategy::Alert::disabled;
	}


}
