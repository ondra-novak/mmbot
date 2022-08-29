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
#include "invert_strategy.h"

#include "sgn.h"
#include "strategy_halfhalf.h"
#include "strategy_keepvalue.h"
#include "strategy_hypersquare.h"
#include "strategy_sinh.h"
#include "strategy_constantstep.h"
#include "strategy_error_fn.h"
#include "strategy_keepbalance.h"
#include "strategy_sinh_val.h"
#include "strategy_gamma.h"
#include "strategy_hedge.h"
#include "strategy_sinh_gen.h"
#include "strategy_leveraged_base.tcc"
#include "strategy_pile.h"
#include "strategy_keepvalue2.h"
#include "strategy_hodl_short.h"
#include "strategy_incvalue.h"
#include "strategy_exponencial.h"



static json::NamedEnum<Strategy_Gamma::Function> strGammaFunction ({
	{Strategy_Gamma::halfhalf,""},
	{Strategy_Gamma::halfhalf,"halfhalf"},
	{Strategy_Gamma::keepvalue,"keepvalue"},
	{Strategy_Gamma::exponencial,"exponencial"},
	{Strategy_Gamma::gauss,"gauss"},
	{Strategy_Gamma::invsqrtsinh,"invsqrtsinh"},
	{Strategy_Gamma::expwide,"expwide"}

});

using ondra_shared::StrViewA;

template<typename Cfg>
void initConfig(Cfg &cfg, json::Value config,
		double power) {
	cfg.power = power;
	cfg.reduction = config["reduction"].getNumber();
	cfg.external_balance = config["extbal"].getNumber();
	cfg.powadj = config["powadj"].getNumber();
	cfg.dynred = config["dynred"].getNumber();
	cfg.open_limit = config["limit"].getNumber();
	cfg.trend_factor = config["trend_factor"].getNumber();
	cfg.longonly = config["longonly"].getBool();
	cfg.recalc_keep_neutral = config["recalc_mode"].getString() == "neutral";
	cfg.fastclose = config["fastclose"].getValueOrDefault(true);
	cfg.slowopen = config["slowopen"].getValueOrDefault(true);
	cfg.reinvest_profit = config["reinvest_profit"].getValueOrDefault(false);
}

Strategy Strategy::create_base(std::string_view id, json::Value config) {

	if (id == Strategy_HalfHalf::id) {
		Strategy_HalfHalf::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_HalfHalf(cfg));
	} else if (id == Strategy_Pile::id) {
		Strategy_Pile::Config cfg;
		cfg.accum = config["accum"].getNumber()*0.01;
		cfg.ratio= config["ratio"].getNumber()*0.01;
        cfg.boost_power= config["bpw"].getNumber()*0.01;
        cfg.boost_volatility= config["bvl"].getNumber();
        if (cfg.boost_volatility) cfg.boost_volatility = 1.0/cfg.boost_volatility;
        cfg.boost_power *= cfg.boost_volatility;
		return Strategy(new Strategy_Pile(cfg));
	} else if (id == Strategy_KeepValue2::id) {
		Strategy_KeepValue2::Config cfg;
		cfg.accum = config["accum"].getNumber()*0.01;
		cfg.ratio= 1.0;
		cfg.reinvest = config["reinvest"].getBool();
		cfg.rebalance = config["boost"].getBool();
		cfg.chngtm = config["chngtm"].getNumber();
		return Strategy(new Strategy_KeepValue2(cfg));
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
    } else if (id == Strategy_Exponencial::id) {
        Strategy_Exponencial::Config cfg;
        cfg.r = config["r"].getNumber()*0.01;
        cfg.z = config["z"].getNumber();
        cfg.w = 1.0-config["w"].getNumber()*0.01;
        cfg.s = config["s"].getNumber()*0.01;
        return Strategy(new Strategy_Exponencial(cfg));
	} else if (id == Strategy_KeepValue::id) {
		Strategy_KeepValue::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		cfg.chngtm = config["valinc"].getNumber();
		return Strategy(new Strategy_KeepValue(cfg,{}));
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
		double curv = config["curv"].getValueOrDefault(5.0);
		return Strategy(new Strategy_SinhVal(std::make_shared<Strategy_SinhVal::TCalc>(power, curv),
			    							std::make_shared<Strategy_SinhVal::Config>(cfg)));
	} else if (id == Strategy_KeepBalance::id) {
		Strategy_KeepBalance::Config cfg;
		cfg.keep_max = config["keep_max"].getNumber();
		cfg.keep_min = config["keep_min"].getNumber();
		return Strategy(new Strategy_KeepBalance(cfg));
	} else if (id == Strategy_Gamma::id) {
		Strategy_Gamma::Config cfg;
		cfg.intTable = std::make_shared<Strategy_Gamma::IntegrationTable>(strGammaFunction[config["function"].getString()],config["exponent"].getNumber());
		cfg.reduction_mode = config["rebalance"].getInt();
		cfg.trend= config["trend"].getNumber();
		cfg.reinvest= config["reinvest"].getNumber();
		cfg.maxrebalance= false;
		return Strategy(new Strategy_Gamma(cfg));
	} else if (id == Strategy_Hedge::id) {
		Strategy_Hedge::Config cfg;
		cfg.h_long = config["long"].getBool();
		cfg.h_short = config["short"].getBool();
		cfg.ptc_drop = config["drop"].getNumber()*0.01;
		return Strategy(new Strategy_Hedge(cfg));
	} else if (id == Strategy_Hodl_Short::id) {
		Strategy_Hodl_Short::Config cfg;
		double acc = config["acc"].getNumber()*0.01;
		bool reinvst = config["rinvst"].getBool();
		double z = config["z"].getNumber();
		cfg.z = z;
		cfg.b = config["b"].getValueOrDefault(100.0)*0.01;
		cfg.reinvest = reinvst;
		cfg.acc = acc;
		return Strategy(new Strategy_Hodl_Short(cfg));
	} else if (id == "passive_income") {
		Strategy_Gamma::Config cfg;
		cfg.intTable = std::make_shared<Strategy_Gamma::IntegrationTable>(Strategy_Gamma::Function::halfhalf,config["exponent"].getNumber());
		cfg.reduction_mode = 4;
		cfg.trend= 0;
		cfg.reinvest=false;
		cfg.maxrebalance = true;
		return Strategy(new Strategy_Gamma(cfg));
	} else if (id == Strategy_Sinh_Gen::id) {
		Strategy_Sinh_Gen::Config cfg;
		cfg.disableSide = config["disableSide"].getInt();
		double p = config["p"].getNumber()*0.01;
		double w = config["w"].getNumber();
		double b = config["b"].getNumber();
		double z = -cfg.disableSide?0:config["z"].getNumber()*0.002;
		cfg.calc = std::make_shared<Strategy_Sinh_Gen::FnCalc>(w,b*0.01,z);
		cfg.power = p;
		cfg.lazyopen = config["lazyopen"].getBool();
		cfg.lazyclose = config["lazyclose"].getBool();
		cfg.reinvest = config["reinvest"].getBool();
		cfg.avgspread= config["avgspread"].getBool();
		cfg.boostmode= config["boostmode"].getUInt();
		cfg.openlimit = config["openlimit"].getNumber();
		cfg.ratio = config["ratio"].getNumber()*0.01;
		return Strategy(new Strategy_Sinh_Gen(cfg));
	} else if (id == Strategy_IncValue::id) {
		Strategy_IncValue::Config cfg;
		cfg.r = config["r"].getNumber()*0.01;
		cfg.w = config["w"].getNumber();
		cfg.fn.z = std::max(config["z"].getNumber(),1.0);
		cfg.ms = config["ms"].getNumber()*0.01;
		cfg.reinvest = config["ri"].getBool();
		return Strategy(new Strategy_IncValue(cfg));
	} else {
		throw std::runtime_error(std::string("Unknown strategy: ").append(id));
	}

}

Strategy Strategy::invert() const {
	return Strategy(new InvertStrategy(ptr));
}

Strategy Strategy::create(std::string_view id, json::Value config) {
	Strategy s = create_base(id, config);
	if (config["invert_proxy"].getBool()) return s.invert();
	else return s;
}


json::Value Strategy::exportState() const {
	return json::Object({{ptr->getID(), ptr->exportState()}});
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


