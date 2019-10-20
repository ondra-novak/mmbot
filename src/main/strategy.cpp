/*
 * strategy.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy.h"

#include <imtjson/object.h>
#include "../shared/stringview.h"
#include "strategy_plfrompos.h"
#include "strategy_halfhalf.h"
#include "strategy_keepvalue.h"

using ondra_shared::StrViewA;
Strategy Strategy::create(std::string_view id, json::Value config) {

	if (id== Strategy_PLFromPos::id) {
		Strategy_PLFromPos::Config cfg;
		cfg.accum = config["accum"].getNumber();
		cfg.step = config["step"].getNumber();
		cfg.neutral_pos = config["neutral_pos"].getNumber();
		return Strategy(new Strategy_PLFromPos(cfg));
	} else if (id == Strategy_HalfHalf::id) {
		double ea = config["ea"].getNumber();
		double accum = config["accum"].getNumber();
		return Strategy(new Strategy_HalfHalf(ea, accum));
	} else if (id == Strategy_KeepValue::id) {
		Strategy_KeepValue::Config cfg;
		cfg.ea = config["ea"].getNumber();
		cfg.accum = config["accum"].getNumber();
		return Strategy(new Strategy_KeepValue(cfg));
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
