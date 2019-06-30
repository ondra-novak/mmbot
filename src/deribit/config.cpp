/*
 * config.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */



#include "config.h"

Config load(const ondra_shared::IniConfig::Section& cfg) {
	Config r;

	r.privKey = cfg.mandatory["secret"].getString();
	r.pubKey = cfg.mandatory["key"].getString();
	r.apiUrl = cfg.mandatory["url"].getString();
	r.scopes = cfg.mandatory["scope"].getString();
	return r;
}


