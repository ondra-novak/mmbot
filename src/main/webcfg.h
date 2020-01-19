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
#include "backtest.h"
#include "traders.h"


class WebCfg {
public:

	using Action = std::function<void()>;
	using Dispatch = ondra_shared::shared_function<void(Action &&)>;

	template<typename T>
	class Cache {
	public:
		using Subj = T;

		Cache() {}
		Cache(const T &t, std::string name, std::chrono::system_clock::time_point expires)
			:t(t),name(name),expires(expires) {}

		const T getSubject() const {return t;}
		bool available(const std::string_view &name, std::chrono::system_clock::time_point time) {
			return name == this->name && time < expires;
		}

	protected:
		T t;
		std::string name;
		std::chrono::system_clock::time_point expires;
	};

	struct SpreadCacheItem {
		MTrader::Chart chart;
		bool invert_price;
	};

	using BacktestCache = Cache<std::vector<BTPrice> >;
	using SpreadCache = Cache<SpreadCacheItem>;

	class State : public ondra_shared::RefCntObj{
	public:
		std::recursive_mutex lock;
		unsigned int write_serial = 0;
		PStorage config;
		ondra_shared::RefCntPtr<AuthUserList> users, admins;
		std::vector<std::string> traderNames;
		json::Value broker_config;
		BacktestCache backtest_cache;
		SpreadCache spread_cache;

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
		void setAdminUser(const std::string &uname, const std::string &pwd);
		ondra_shared::linear_set<std::string> logout_users;

		void logout_user(std::string &&user);
		bool logout_commit(std::string &&user);
		void setBrokerConfig(json::StrViewA name, json::Value config);
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
		login,
		backtest,
		spread
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
	bool reqLogin(simpleServer::HTTPRequest req) const;
	bool reqBrokerSpec(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest, IStockApi *api, ondra_shared::StrViewA broker_name) const;
	bool reqEditor(simpleServer::HTTPRequest req) const;
	bool reqBacktest(simpleServer::HTTPRequest req) const;
	bool reqSpread(simpleServer::HTTPRequest req) const;

	using Sync = std::unique_lock<std::recursive_mutex>;



	ondra_shared::RefCntPtr<State> state;
};



#endif /* SRC_MAIN_WEBCFG_H_ */
