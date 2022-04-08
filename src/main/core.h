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
#include "auth.h"

#include "traders.h"

class BotCore {
public:

	BotCore(const ondra_shared::IniConfig &cfg);
	~BotCore();
	void run();
	void setConfig(json::Value cfg);



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
	ondra_shared::Countdown in_progress;


	void applyConfig(json::Value cfg);
	void init_cycle(std::chrono::steady_clock::time_point next_start);

};



#endif /* SRC_MAIN_CORE_H_ */
