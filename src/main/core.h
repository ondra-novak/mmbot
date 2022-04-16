/*
 * core.h
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_CORE_H_
#define SRC_MAIN_CORE_H_

#include <shared/scheduler.h>
#include <shared/worker.h>
#include <shared/countdown.h>
#include "../shared/semaphore.h"
#include "backtest_broker.h"

#include "abstractExtern.h"

#include "btstore.h"

#include "auth.h"

#include "traders.h"



class BotCore {
public:

	BotCore(const ondra_shared::IniConfig &cfg);
	~BotCore();
	void run(bool suspended);
	void setConfig(json::Value cfg);
	void storeConfig(json::Value cfg);

	PAuthService get_auth() {
		return authService;
	}

	PBalanceMap get_balance_cache()  {
		return balanceCache;
	}

	const json::Value& get_broker_config() const {
		return broker_config;
	}

	PBrokerList get_broker_list() {
		return brokerList;
	}

	PBalanceMap get_ext_balance() {
		return extBalance;
	}

	const PHistStorageFactory<HistMinuteDataItem>& get_hist_storage_factory() const {
		return histStorageFactory;
	}


	const PPerfModule& get_perfmod() const {
		return perfmod;
	}

	PReport get_report() const {
		return rpt;
	}

	const MemStorage *get_rpt_storage() const {
		return rptStorage;
	}

	const ondra_shared::Scheduler& get_scheduler() const {
		return sch;
	}

	const PStorageFactory& get_storage_factory() const {
		return storageFactory;
	}

	const PTraders& get_traders() const {
		return traders;
	}

	const PUtilization& get_utlz() const {
		return utlz;
	}

	const ondra_shared::Worker& get_worker() const {
		return wrk;
	}

	auto &get_cycle_lock() {
		return in_progres;
	}

	json::Value get_config() const;

	PBacktestStorage get_backtest_storage() const {
		return backtest_storage;
	}

	const PBacktestBroker &get_backtest_broker() const {
		return backtest_broker;
	}

protected:



	ondra_shared::Scheduler sch;
	ondra_shared::Worker wrk;
	PBrokerList brokerList;
	PStorageFactory storageFactory;
	PHistStorageFactory<HistMinuteDataItem> histStorageFactory;
	PBalanceMap balanceCache;
	PBalanceMap extBalance;
	MemStorage *rptStorage;
	PReport rpt;
	PPerfModule perfmod;
	PAuthService authService;
	PUtilization utlz;
	json::Value broker_config;
	json::Value news_tm;
	PTraders traders;
	PStorage cfg_storage;
	PBacktestStorage backtest_storage;
	PBacktestBroker backtest_broker;
	ondra_shared::Semaphore in_progres;


	void applyConfig(json::Value cfg);
	void init_cycle(std::chrono::steady_clock::time_point next_start);

};





#endif /* SRC_MAIN_CORE_H_ */
