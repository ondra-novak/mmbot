/*
 * webcfg.h
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_WEBCFG_H_
#define SRC_MAIN_WEBCFG_H_

#include <shared/stringview.h>
#include <shared/shared_function.h>
#include <simpleServer/http_parser.h>
#include <imtjson/namedEnum.h>
#include <mutex>

#include <shared/ini_config.h>
#include "../server/src/rpc/rpcServer.h"
#include "istockapi.h"
#include "authmapper.h"
#include "traders.h"


class WebCfg {
public:

	using Action = std::function<void()>;
	using Dispatch = ondra_shared::shared_function<void(Action &&)>;


	class State : public ondra_shared::RefCntObj{
	public:
		std::recursive_mutex lock;
		unsigned int write_serial = 0;
		PStorage config;
		ondra_shared::RefCntPtr<AuthUserList> users, admins;
		std::vector<std::string> traderNames;

		State( PStorage &&config,
			  ondra_shared::RefCntPtr<AuthUserList> users,
			  ondra_shared::RefCntPtr<AuthUserList> admins):
				  config(std::move(config)),
				  users(users),
				  admins(admins) {}

		~State() {
			lock.lock();
			lock.unlock();
		}

		void init();
		void init(json::Value v);
		void applyConfig(Traders &t);
		void setAdminAuth(json::StrViewA auth);
		ondra_shared::linear_set<std::string> logout_users;

		void logout_user(std::string &&user);
		bool logout_commit(std::string &&user);
	};


	WebCfg( ondra_shared::RefCntPtr<State> state,
			const std::string &realm,
			Traders &traders,
			Dispatch &&dispatch);

	~WebCfg();

	bool operator()(const simpleServer::HTTPRequest &req,  const ondra_shared::StrViewA &vpath) const;


	enum Command {
		config,
		serialnr,
		brokers,
		traders,
		stop,
		logout,
		logout_commit,
		editor,
	};

	AuthMapper auth;
	Traders &trlist;
	Dispatch dispatch;
	unsigned int serial;

	static json::NamedEnum<Command> strCommand;


protected:
	bool reqConfig(simpleServer::HTTPRequest req) const;
	bool reqSerial(simpleServer::HTTPRequest req) const;
	bool reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) const;
	bool reqTraders(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) const;
	bool reqLogout(simpleServer::HTTPRequest req, bool commit) const;
	bool reqStop(simpleServer::HTTPRequest req) const;
	bool reqBrokerSpec(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest, IStockApi *api) const;
	bool reqEditor(simpleServer::HTTPRequest req) const;

	using Sync = std::unique_lock<std::recursive_mutex>;



	ondra_shared::RefCntPtr<State> state;
};



#endif /* SRC_MAIN_WEBCFG_H_ */
