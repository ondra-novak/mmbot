/*
 * config.h
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_COINMATE_CONFIG_H_
#define SRC_COINMATE_CONFIG_H_
#include "../shared/ini_config.h"



struct Config {
		std::string apiUrl;
		std::string privKey;
		std::string pubKey;
		std::string scopes;

	};

Config load(const ondra_shared::IniConfig::Section &cfg);

#endif /* SRC_COINMATE_CONFIG_H_ */
