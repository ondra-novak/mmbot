/*
 * strategy.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy.h"

#include <imtjson/namedEnum.h>
#include <imtjson/object.h>
#include "../shared/stringview.h"
#include "strategy_plfrompos.h"
#include "strategy_halfhalf.h"
#include "strategy_keepvalue.h"

static json::NamedEnum<Strategy_PLFromPos::CloseMode> strCloseMode ({
		{Strategy_PLFromPos::always_close,"always_close"},
		{Strategy_PLFromPos::prefer_close,"prefer_close"},
		{Strategy_PLFromPos::prefer_reverse,"prefer_reverse"}
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

double IStrategy::calcOrderSize(double expectedAmount, double actualAmount, double newAmount) {
	double middle = (actualAmount + expectedAmount)/2;
	double size = newAmount - middle;
	return size;
}

