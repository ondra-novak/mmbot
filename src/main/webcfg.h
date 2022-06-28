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
#include "../server/src/simpleServer/query_parser.h"
#include "abstractExtern.h"
#include "istockapi.h"
#include "authmapper.h"
#include "backtest.h"
#include "btstore.h"
#include "traders.h"


class WebCfg {
public:

	using Action = std::function<void()>;
	using Dispatch = ondra_shared::shared_function<void(Action &&)>;
	struct Users {
		ondra_shared::RefCntPtr<AuthUserList> users, admins, reports;
	};

	template<typename T>
	class Cache {
	public:
		using Subj = T;

		Cache() {}
		Cache(const T &t, std::string name)
			:t(t),name(name) {}

		const T getSubject() const {return t;}
		bool available(const std::string_view &name) const {
			return name == this->name;
		}
		void clear() {
			name.clear();
			t = T();
		}

	protected:
		T t;
		std::string name;
	};

	struct SpreadCacheItem {
		MTrader::Chart chart;
		bool invert_price;
	};

	struct BacktestCacheSubj {
		std::vector<BTPrice> prices;
		IStockApi::MarketInfo minfo;
		bool reversed;
		bool inverted;
	};

	using BacktestCache = Cache<BacktestCacheSubj>;
	using SpreadCache = Cache<SpreadCacheItem>;
	using PricesCache = Cache<std::vector<double> >;

	class State {
	public:
		unsigned int write_serial = 0;
		PStorage config;
		Users users;
		std::vector<std::string> traderNames;
		json::Value broker_config;
		BacktestCache backtest_cache;
		SpreadCache spread_cache;
		PricesCache prices_cache;
		std::map<std::size_t,std::pair<json::Value,bool> > progress_map;
		shared_lockable_ptr<BacktestStorage> backtest_storage;
		std::string news_url;
		json::Value news_tm;

		State( PStorage &&config,
			  Users users,
			  std::size_t backtest_cache_size,
			  bool backtest_in_memory,
			  std::string news_url
			):
				  config(std::move(config)),
				  users(users),
				  backtest_storage(shared_lockable_ptr<BacktestStorage>::make(backtest_cache_size,backtest_in_memory)),
				  news_url(news_url)
		{
		}


		void init();
		void init(json::Value v);
		void applyConfig(shared_lockable_ptr<Traders> &t);
		void setAdminUser(const std::string &uname, const std::string &pwd);
		ondra_shared::linear_set<std::string> logout_users;

		void logout_user(std::string &&user);
		bool logout_commit(std::string &&user);
		void setBrokerConfig(const std::string_view &name, json::Value config);
		void initProgress(std::size_t i);
		bool setProgress(std::size_t i, json::Value v);
		void clearProgress(std::size_t i);
		json::Value getProgress(std::size_t i) const;
		void stopProgress(std::size_t i) ;

		json::Value loadNews(bool all) const;
		bool isNewsConfigured() const;
		void markNewsRead(json::Value tm);
	};


	class Progress {
	public:
		shared_lockable_ptr<State> state;
		std::size_t id;
		Progress(const shared_lockable_ptr<State> &state, std::size_t id);
		Progress(Progress &&s);
		Progress(const Progress &s);
		~Progress();
		bool set(json::Value v);
	};

	WebCfg( const shared_lockable_ptr<State> &state,
			const std::string &realm,
			const shared_lockable_ptr<Traders> &traders,
			Dispatch &&dispatch,
			json::PJWTCrypto jwt,
			shared_lockable_ptr<AbstractExtern> backtest_broker,
			std::size_t upload_limit
	);

	~WebCfg();

	bool operator()(const simpleServer::HTTPRequest &req,  const ondra_shared::StrViewA &vpath);


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
		backtest2,
		spread,
		strategy,
		wallet,
		btdata,
		visstrategy,
		utilization,
		progress,
		news
	};

	AuthMapper auth;
	shared_lockable_ptr<Traders> trlist;
	Dispatch dispatch;
	unsigned int serial;

	static json::NamedEnum<Command> strCommand;


protected:
	bool reqConfig(simpleServer::HTTPRequest req);
	bool reqSerial(simpleServer::HTTPRequest req);
	bool reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest);
	bool reqTraders(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest);
	bool reqLogout(simpleServer::HTTPRequest req, bool commit);
	bool reqStop(simpleServer::HTTPRequest req);
	bool reqLogin(simpleServer::HTTPRequest req);
	bool reqBrokerSpec(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest, PStockApi api, ondra_shared::StrViewA broker_name);
	bool reqEditor(simpleServer::HTTPRequest req);
	bool reqBacktest(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest);
	bool reqSpread(simpleServer::HTTPRequest req);
	bool reqStrategy(simpleServer::HTTPRequest req);
	bool reqDumpWallet(simpleServer::HTTPRequest req, ondra_shared::StrViewA vpath);
	bool reqBTData(simpleServer::HTTPRequest req);
	bool reqVisStrategy(simpleServer::HTTPRequest req,  simpleServer::QueryParser &qp);
	bool reqUtilization(simpleServer::HTTPRequest req,  simpleServer::QueryParser &qp);
	bool reqProgress(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest);
	bool reqNews(simpleServer::HTTPRequest req);

	using Sync = std::unique_lock<std::recursive_mutex>;

	using PState = shared_lockable_ptr<State>;

	PState state;
	shared_lockable_ptr<AbstractExtern> backtest_broker;
	std::size_t upload_limit;


	bool reqBacktest_v2(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest);

	static void processBrokerHistory(simpleServer::HTTPRequest req,
			PState state, PStockApi api, ondra_shared::StrViewA pair
	);
	struct DataDownloaderTask;
};



#endif /* SRC_MAIN_WEBCFG_H_ */
