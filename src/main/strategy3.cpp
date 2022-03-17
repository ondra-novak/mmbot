/*
 * strategy3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include "strategy3.h"

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
