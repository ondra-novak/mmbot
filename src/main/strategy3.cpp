/*
 * strategy3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include "strategy3.h"


json::NamedEnum<OrderRequestResult> Strategy3::strOrderRequestResult({
	{OrderRequestResult::accepted,"Accepted"},
	{OrderRequestResult::partially_accepted,"Partially accepted"},
	{OrderRequestResult::invalid_price,"Invalid price"},
	{OrderRequestResult::invalid_size,"Invalid size"},
	{OrderRequestResult::max_leverage,"Max leverage reached"},
	{OrderRequestResult::no_funds,"No funds"},
	{OrderRequestResult::too_small,"Too small order"},
	{OrderRequestResult::max_position,"Max position reached"},
	{OrderRequestResult::min_position,"Min position reached"},
	{OrderRequestResult::min_position,"Max costs reached"},

});

Strategy3 StrategyRegister::create(std::string_view id, json::Value config) {
	auto iter = smap.find(id);
	if (iter == smap.end()) throw UnknownStrategyException(id);
	return Strategy3(iter->second->create(config));
}

void StrategyRegister::reg_strategy(std::unique_ptr<AbstractStrategyFactory> &&factory) {
	auto id = factory->get_id();
	smap.emplace(id, std::move(factory));
}

StrategyRegister& StrategyRegister::getInstance() {
	static StrategyRegister strg;
	return strg;
}

const char* UnknownStrategyException::what() const noexcept {
	if (msg.empty()) {
		msg = "Unknown strategy: ";
		msg.append(id);
	}
	return msg.c_str();
}

UnknownStrategyException::UnknownStrategyException(std::string_view id):id(id) {
}
