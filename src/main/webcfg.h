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
#include <shared/ini_config.h>
#include "istockapi.h"
#include "authmapper.h"


class WebCfg {
public:

	using Action = std::function<void()>;
	using Dispatch = std::function<void(Action &&)>;


	WebCfg(const ondra_shared::IniConfig::Section &cfg,
			const std::string &realm,
			IStockSelector &stockSelector,
			Action &&restart_fn,
			Dispatch &&dispatch);


	bool operator()(const simpleServer::HTTPRequest &req,  const ondra_shared::StrViewA &vpath) const;


	enum Command {
		config,
		restart,
		serialnr,
		brokers
	};

	AuthMapper auth;
	std::string config_path;
	IStockSelector &stockSelector;
	Action restart_fn;
	Dispatch dispatch;
	unsigned int serial;

	static json::NamedEnum<Command> strCommand;

protected:
	bool reqConfig(simpleServer::HTTPRequest req) const;
	bool reqRestart(simpleServer::HTTPRequest req) const;
	bool reqSerial(simpleServer::HTTPRequest req) const;
	bool reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) const;


};



#endif /* SRC_MAIN_WEBCFG_H_ */
