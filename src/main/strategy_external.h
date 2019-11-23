/*
 * strategy_external.h
 *
 *  Created on: 23. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_EXTERNAL_H_
#define SRC_MAIN_STRATEGY_EXTERNAL_H_

#include <imtjson/value.h>
#include "abstractExtern.h"
#include "istrategy.h"

class StrategyExternal: public AbstractExtern {
public:
	using AbstractExtern::AbstractExtern;

	PStrategy createStrategy(const std::string_view &id, json::Value config);


	class Strategy;


};

#endif /* SRC_MAIN_STRATEGY_EXTERNAL_H_ */
