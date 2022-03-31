/*
 * trader_factory.cpp
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */

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



}
