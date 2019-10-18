/*
 * strategy.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy.h"

#include "../shared/stringview.h"
#include "strategy_plfrompos.h"
#include "strategy_halfhalf.h"

using ondra_shared::StrViewA;
Strategy Strategy::create(StrViewA name, json::Value config) {

	if (name == "plfrompos") {
		Strategy_PLFromPos::Config cfg;
		cfg.accum = config["accum"].getNumber();
		cfg.step = config["step"].getNumber();
		return Strategy(new Strategy_PLFromPos(cfg));
	} else if (name == "halfhalf") {
		double ea = config["ea"].getNumber();
		double accum = config["accum"].getNumber();
		return Strategy(new Strategy_HalfHalf(ea, accum));
	} else {
		throw std::runtime_error(std::string("Unknown strategy: ").append(name.data, name.length));
	}

}

