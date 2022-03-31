/*
 * trader_factory.h
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADER_FACTORY_H_
#define SRC_MAIN_TRADER_FACTORY_H_
#include "trader.h"



///Trader_Config_Ex contains Trader_Config, however it exposes spread_id and config and strategy_id and config
/**
 * This allows to parse complete trader's config
 */
struct Trader_Config_Ex: public Trader_Config {
	json::Value strategy_config;
	json::Value spread_config;
	std::string strategy_id;
	std::string spread_id;

	void parse(json::Value cfg);
};



#endif /* SRC_MAIN_TRADER_FACTORY_H_ */
