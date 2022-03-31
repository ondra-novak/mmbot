
/*
 * spreadgenerator.cpp
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */


#include <stdexcept>
#include "spreadgenerator.h"



SpreadGenerator SpreadRegister::create(std::string_view id, json::Value config) {
	auto iter = smap.find(id);
	if (iter == smap.end()) throw std::runtime_error(std::string("Invalid spread generator: ").append(id));
	return iter->second->create(config);
}

void SpreadRegister::reg(std::unique_ptr<ISpreadGeneratorFactory> &&factory) {
	auto id = factory->get_id();
	smap.emplace(id, std::move(factory));
}

SpreadRegister& SpreadRegister::getInstance() {
	static SpreadRegister inst;
	return inst;
}
