/*
 * config.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */



#include "config.h"

Config load(const ondra_shared::IniConfig::Section& cfg) {
	Config r;

	r.privKey = cfg.mandatory["private_key"].getString();
	r.pubKey = cfg.mandatory["public_key"].getString();
	r.clientid = cfg.mandatory["clientid"].getString();
	r.apiUrl= cfg.mandatory["api_url"].getString();
	return r;
}


