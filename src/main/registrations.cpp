/*
 * registrations.cpp
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */

#include "registrations.h"

#include "default_spread_generator.h"
#include "spreadgenerator.h"
#include "strategy3.h"
#include "strategy_pile.h"


void init_registrations() {
	StrategyRegister &strg = StrategyRegister::getInstance();
	Strategy3_Pile::reg(strg);


	SpreadRegister &sprg = SpreadRegister::getInstance();
	AdaptiveSpreadGenerator::reg(sprg);
	FixedSpreadGenerator::reg(sprg);
}


