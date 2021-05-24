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
#include "strategy_keepbalance.h"
#include "strategy_sinh_val.h"
#include "strategy_martingale.h"
#include "strategy_gamma.h"



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

static json::NamedEnum<Strategy_Gamma::Function> strGammaFunction ({
	{Strategy_Gamma::halfhalf,""},
	{Strategy_Gamma::halfhalf,"halfhalf"},
	{Strategy_Gamma::keepvalue,"keepvalue"},
	{Strategy_Gamma::exponencial,"exponencial"},

});

using ondra_shared::StrViewA;

template<typename Cfg>
void initConfig(Cfg &cfg, json::Value config,
		double power) {
	cfg.power = power;
	cfg.asym = config["asym"].getNumber();
	cfg.reduction = config["reduction"].getNumber();
	cfg.external_balance = config["extbal"].getNumber();
	cfg.powadj = config["powadj"].getNumber();
	cfg.dynred = config["dynred"].getNumber();
	cfg.initboost = config["initboost"].getNumber();
	cfg.trend_factor = config["trend_factor"].getNumber();
	cfg.longonly = config["longonly"].getBool();
	cfg.recalc_keep_neutral = config["recalc_mode"].getString() == "neutral";
	cfg.fastclose = config["fastclose"].getValueOrDefault(true);
	cfg.slowopen = config["slowopen"].getValueOrDefault(true);
	cfg.reinvest_profit = config["reinvest_profit"].getValueOrDefault(false);
}

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
		return Strategy(new Strategy_Exponencial(cfg));
	} else if (id == Strategy_HyperSquare::id) {
		Strategy_HyperSquare::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_HyperSquare(cfg));
	} else if (id == Strategy_ConstantStep::id) {
		Strategy_ConstantStep::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_ConstantStep(cfg));
	} else if (id == Strategy_ErrorFn::id) {
		Strategy_ErrorFn::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.rebalance.hi_a = config["rb_hi_a"].getValueOrDefault(0.5);
		cfg.rebalance.lo_a = config["rb_lo_a"].getValueOrDefault(0.5);
		cfg.rebalance.hi_p = config["rb_hi_p"].getValueOrDefault(0.90);
		cfg.rebalance.lo_p = config["rb_lo_p"].getValueOrDefault(0.20);
		return Strategy(new Strategy_ErrorFn(cfg));
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
	} else if (id == Strategy_Hyperbolic::id) {
		Strategy_Hyperbolic::Config cfg;
		initConfig(cfg, config, config["power"].getNumber());
		return Strategy(new Strategy_Hyperbolic(std::make_shared<Strategy_Hyperbolic::TCalc>(),
											    std::make_shared<Strategy_Hyperbolic::Config>(cfg)));
	} else if (id == Strategy_Linear::id) {
		Strategy_Linear::Config cfg;
		initConfig(cfg, config, config["power"].getNumber());
		return Strategy(new Strategy_Linear(std::make_shared<Strategy_Linear::TCalc>(),
			    							std::make_shared<Strategy_Linear::Config>(cfg)));
	} else if (id == Strategy_Sinh::id) {
		Strategy_Sinh::Config cfg;
		double power = config["power"].getNumber();
		initConfig(cfg, config, power);
		double curv = config["curv"].getValueOrDefault(5.0);
		return Strategy(new Strategy_Sinh(std::make_shared<Strategy_Sinh::TCalc>(power, curv),
			    							std::make_shared<Strategy_Sinh::Config>(cfg)));
	} else if (id == Strategy_Sinh2::id) {
		Strategy_Sinh2::Config cfg;
		double power = std::exp(config["power"].getNumber());
		initConfig(cfg, config, power);
		double curv = config["curv"].getValueOrDefault(5.0);
		return Strategy(new Strategy_Sinh2(std::make_shared<Strategy_Sinh2::TCalc>(curv),
			    							std::make_shared<Strategy_Sinh2::Config>(cfg)));
	} else if (id == Strategy_SinhVal::id) {
		Strategy_SinhVal::Config cfg;
		double power = config["power"].getNumber();
		initConfig(cfg,config,power);
		cfg.asym = 0;
		double curv = config["curv"].getValueOrDefault(5.0);
		return Strategy(new Strategy_SinhVal(std::make_shared<Strategy_SinhVal::TCalc>(power, curv),
			    							std::make_shared<Strategy_SinhVal::Config>(cfg)));
	} else if (id == Strategy_KeepBalance::id) {
		Strategy_KeepBalance::Config cfg;
		cfg.keep_max = config["keep_max"].getNumber();
		cfg.keep_min = config["keep_min"].getNumber();
		return Strategy(new Strategy_KeepBalance(cfg));
	} else if (id == Strategy_Martingale::id) {
		Strategy_Martingale::Config cfg;
		cfg.initial_step = config["initial_step"].getNumber();
		cfg.power = config["power"].getNumber();
		cfg.reduction = config["reduction"].getNumber();
		cfg.collateral= config["collateral"].getNumber();
		cfg.allow_short = config["allow_short"].getBool();
		return Strategy(new Strategy_Martingale(cfg));
	} else if (id == Strategy_Gamma::id) {
		Strategy_Gamma::Config cfg;
		cfg.intTable = std::make_shared<Strategy_Gamma::IntegrationTable>(strGammaFunction[config["function"].getString()],config["exponent"].getNumber());
		cfg.reduction_mode = config["rebalance"].getInt();
		cfg.trend= config["trend"].getNumber();
		return Strategy(new Strategy_Gamma(cfg));
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
		if (order.alert == IStrategy::Alert::enabled && !enable_alerts)
			order.alert = IStrategy::Alert::disabled;
		order.size = 0;
	} else if (order.alert == IStrategy::Alert::forced) {
		order.alert = IStrategy::Alert::enabled;
	}
}
