/*
 * trader_factory.cpp
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <imtjson/string.h>
#include "trader_factory.h"

void Trader_Config_Ex::parse(json::Value data) {
	pairsymb = data["pair_symbol"].getString();
	broker = data["broker"].getString();
	title = data["title"].getString();

	auto strdata = data["strategy"];
	strategy_id= strdata["type"].toString();
	strategy_config = strdata;

	auto sprdata = data["spread"];
	spread_id = sprdata["type"].toString();
	spread_config = sprdata;
	swap_mode = static_cast<SwapMode3>(data["swap_symbols"].getUInt());


	min_size = data["min_size"].getValueOrDefault(0.0);
	max_size = data["max_size"].getValueOrDefault(0.0);
	json::Value min_balance = data["min_balance"];
	json::Value max_balance = data["max_balance"];
	json::Value max_costs = data["max_costs"];
	if (min_balance.type() == json::number) this->min_position= min_balance.getNumber();
	if (max_balance.type() == json::number) this->max_position= max_balance.getNumber();
	if (max_costs.type() == json::number) this->max_costs = max_costs.getNumber();

	max_leverage = data["max_leverage"].getValueOrDefault(0.0);

	report_order = data["report_order"].getValueOrDefault(0.0);

	paper_trading = data["paper_trading"].getValueOrDefault(false);
	dont_allocate = data["dont_allocate"].getValueOrDefault(false) ;
	enabled= data["enabled"].getValueOrDefault(true);
	hidden = data["hidden"].getValueOrDefault(false);
	trade_within_budget = data["trade_within_budget"].getBool();
	init_open = data["init_open"].getNumber();

	if (paper_trading) {
		paper_trading_src_state = data["pp_source"].getString();
	}

	json::Value rset = data["reset"];
	json::Value tmp;

	reset.revision = data["reset_revision"].getUInt();
	bool reset_alloc_pos_100 = data["reset_alloc_position_100"].getBool();
	bool reset_alloc_cur_100 = data["reset_alloc_currency_100"].getBool();
	bool reset_set_position = data["reset_set_position"].getBool();

	if (reset_alloc_pos_100) reset.alloc_position = data["reset_alloc_position"].getNumber();
	if (reset_alloc_cur_100) reset.alloc_currency = data["reset_alloc_currencty"].getNumber();
	if (reset_set_position) {
		reset.trade_optimal_position = data["reset_set_optimal_position"].getBool();
		reset.trade_position= data["reset_set_position_value"].getNumber();
	}

}


json::Value get_trader_form() {
	using namespace json;

	static json::Value def = json::Value::fromString(R"json(
	[{"category":"general","name":"title","type":"string"},
	 {"category":"general","name":"enabled","type":"checkbox","default":true},
	 {"category":"general","name":"hidden","type":"checkbox","default":false},
	 {"category":"general","name":"dont_allocate","type":"checkbox","default":false},
	 {"category":"init","name":"reset_revision","type":"rev_checkbox","default":false},
	 {"category":"init","name":"reset_alloc_position_100","type":"checkbox","default":true,"hideif":{"reset_revision":false}},		
	 {"category":"init","name":"reset_alloc_position","type":"number","default":0,"hideif":{"reset_revision":false,"reset_alloc_position_100":true}},		
	 {"category":"init","name":"reset_alloc_currencty_100","type":"checkbox","default":true,"hideif":{"reset_revision":false}},		
	 {"category":"init","name":"reset_alloc_currencty","type":"number","default":0,"hideif":{"reset_revision":false,"reset_alloc_currencty_100":true}},		
	 {"category":"init","name":"reset_keep_position","type":"checkbox","default":true,"hideif":{"reset_revision":false}},		
	 {"category":"init","name":"reset_set_optimal_position","type":"checkbox","default":true,"hideif":{"reset_revision":false,"reset_keep_position":true}},		
	 {"category":"init","name":"reset_set_position_value","type":"number","default":0,"hideif":{"reset_revision":false,"reset_keep_position":true,"reset_set_optimal_position":true}},
	 {"category":"constrains","name":"min_size","type":"number","min":0,"default":0},		
	 {"category":"constrains","name":"max_size","type":"number","min":0,"default":0},		
	 {"category":"constrains","name":"min_pos","type":"number"},		
	 {"category":"constrains","name":"max_pos","type":"number"},		
	 {"category":"constrains","name":"max_costs","type":"number"},		
	 {"category":"constrains","name":"max_leverage","type":"number","min":0,"default":4},
	 {"category":"constrains","name":"trade_with_budget","type":"checkbox","default":false},
	 {"category":"misc","name":"init_open","type":"number","default":0},
	 {"category":"misc","name":"report_order","type":"number","default":0}
	])json");

	return def;
}
