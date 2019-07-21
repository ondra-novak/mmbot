/*
 * webcfg.h
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_WEBCFG_H_
#define SRC_MAIN_WEBCFG_H_

#include <shared/stringview.h>
#include <simpleServer/http_parser.h>
#include <imtjson/namedEnum.h>
#include "istockapi.h"
#include "authmapper.h"


class WebCfg {
public:



	WebCfg(const std::string &auth,
			const std::string &realm,
			const std::string &config_path,
			unsigned int serial,
			IStockSelector &stockSelector,
			std::function<void()> &&restart_fn);


	bool operator()(const simpleServer::HTTPRequest &req,  const ondra_shared::StrViewA &vpath) const;


	enum Command {
		all_pairs,
		config,
		restart,
		serialnr,
		info
	};

	AuthMapper auth;
	std::string config_path;
	IStockSelector &stockSelector;
	std::function<void()> restart_fn;
	unsigned int serial;

	static json::NamedEnum<Command> strCommand;

protected:
	bool reqAllPairs(simpleServer::HTTPRequest req) const;
	bool reqConfig(simpleServer::HTTPRequest req) const;
	bool reqRestart(simpleServer::HTTPRequest req) const;
	bool reqSerial(simpleServer::HTTPRequest req) const;
	bool reqInfo(simpleServer::HTTPRequest req, ondra_shared::StrViewA broker, ondra_shared::StrViewA symbol) const;


};



#endif /* SRC_MAIN_WEBCFG_H_ */
