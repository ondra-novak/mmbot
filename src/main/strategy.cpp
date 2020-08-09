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
#include "sgn.h"
#include "strategy_halfhalf.h"
#include "strategy_keepvalue.h"
#include "strategy_stairs.h"
#include "strategy_hyperbolic.h"
#include "strategy_exponencial.h"
#include "strategy_hypersquare.h"
#include "strategy_sinh.h"
#include "strategy_constantstep.h"
#include "strategy_error_fn.h"



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

	if (id == Strategy_HalfHalf::id) {
		Strategy_HalfHalf::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_HalfHalf(cfg));
	} else if (id == Strategy_Exponencial::id) {
		Strategy_Exponencial::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.optp = config["optp"].getNumber();
		return Strategy(new Strategy_Exponencial(cfg));
	} else if (id == Strategy_HyperSquare::id) {
		Strategy_HyperSquare::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.optp = config["optp"].getNumber();
		return Strategy(new Strategy_HyperSquare(cfg));
	} else if (id == Strategy_ConstantStep::id) {
		Strategy_ConstantStep::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.optp = config["optp"].getNumber();
		return Strategy(new Strategy_ConstantStep(cfg));
	} else if (id == Strategy_ErrorFn::id) {
		Strategy_ErrorFn::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.rebalance.hi_a = config["rb_hi_a"].getValueOrDefault(0.5);
		cfg.rebalance.lo_a = config["rb_lo_a"].getValueOrDefault(2.0);
		cfg.rebalance.hi_p = config["rb_hi_p"].getValueOrDefault(0.90);
		cfg.rebalance.lo_p = config["rb_lo_p"].getValueOrDefault(0.20);
		return Strategy(new Strategy_ErrorFn(cfg));
	} else if (id == Strategy_KeepValue::id) {
		Strategy_KeepValue::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.chngtm = config["valinc"].getNumber();
		cfg.keep_half=config["halfhalf"].getBool();
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
	} else if (id == Strategy_Hyperbolic::id) {
		Strategy_Hyperbolic::Config cfg;
		cfg.power = config["power"].getNumber();
		cfg.max_loss = config["max_loss"].getNumber();
		cfg.asym = config["asym"].getNumber()/cfg.power;
		cfg.reduction = config["reduction"].getNumber();
		cfg.external_balance = config["extbal"].getNumber();
		cfg.powadj = config["powadj"].getNumber();
		cfg.dynred = config["dynred"].getNumber();
		cfg.initboost = config["initboost"].getNumber();
		cfg.detect_trend = config["dtrend"].getBool();
		cfg.longonly = config["longonly"].getBool();
		cfg.recalc_keep_neutral = config["recalc_mode"].getString() == "neutral";
		return Strategy(new Strategy_Hyperbolic(std::make_shared<Strategy_Hyperbolic::TCalc>(),
											    std::make_shared<Strategy_Hyperbolic::Config>(cfg)));
	} else if (id == Strategy_Linear::id) {
		Strategy_Linear::Config cfg;
		cfg.power = config["power"].getNumber();
		cfg.max_loss = config["max_loss"].getNumber();
		cfg.asym = config["asym"].getNumber()/cfg.power;
		cfg.reduction = config["reduction"].getNumber();
		cfg.external_balance = config["extbal"].getNumber();
		cfg.powadj = config["powadj"].getNumber();
		cfg.dynred = config["dynred"].getNumber();
		cfg.initboost = config["initboost"].getNumber();
		cfg.detect_trend = config["dtrend"].getBool();
		cfg.longonly = config["longonly"].getBool();
		cfg.recalc_keep_neutral = config["recalc_mode"].getString() == "neutral";
		return Strategy(new Strategy_Linear(std::make_shared<Strategy_Linear::TCalc>(),
			    							std::make_shared<Strategy_Linear::Config>(cfg)));
	} else if (id == Strategy_Sinh::id) {
		Strategy_Sinh::Config cfg;
		double power = config["power"].getNumber();
		cfg.max_loss = config["max_loss"].getNumber();
		cfg.power = power;
		cfg.asym = config["asym"].getNumber();
		cfg.reduction = config["reduction"].getNumber();
		cfg.external_balance = config["extbal"].getNumber();
		cfg.powadj = config["powadj"].getNumber();
		cfg.dynred = config["dynred"].getNumber();
		cfg.initboost = config["initboost"].getNumber();
		cfg.detect_trend = config["dtrend"].getBool();
		cfg.longonly = config["longonly"].getBool();
		cfg.recalc_keep_neutral = config["recalc_mode"].getString() == "neutral";
		return Strategy(new Strategy_Sinh(std::make_shared<Strategy_Sinh::TCalc>(power),
			    							std::make_shared<Strategy_Sinh::Config>(cfg)));
	} else {
		throw std::runtime_error(std::string("Unknown strategy: ").append(id));
	}

}

json::Value Strategy::exportState() const {
	return json::Object(ptr->getID(), ptr->exportState());
}

void Strategy::importState(json::Value src, const IStockApi::MarketInfo &minfo) {
	json::Value data = src[ptr->getID()];
	ptr = ptr->importState(data, minfo);
}

double IStrategy::calcOrderSize(double , double actualAmount, double newAmount) {
	double my_diff = newAmount - actualAmount;
/*	double org_diff = newAmount - expectedAmount;
	double my_diff = newAmount - actualAmount;
	if (my_diff * org_diff > 0) {
		double middle = (actualAmount + expectedAmount)/2;
		double size = newAmount - middle;
		return size;
	} else {*/
		return my_diff;
/*	}*/
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
