/*
 * trades.h
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADERS_H_
#define SRC_MAIN_TRADERS_H_

#include <shared/linear_map.h>
#include <shared/worker.h>

#include "report.h"
#include "brokerlist.h"
#include "idailyperfmod.h"
#include "utilization.h"
#include "hist_data_storage.h"
#include "trader.h"



class Traders {
public:

	Traders(PBrokerList brokers,
			PStorageFactory sf,
			PHistStorageFactory<HistMinuteDataItem> hsf,
			PReport rpt,
			PPerfModule perfMod,
			PBalanceMap balanceCache,
			PBalanceMap extBalances);

	void run_cycle(ondra_shared::Worker wrk, PUtilization utls, userver::Callback<void(bool)> &&done);
	void stop_cycle();

	///Initialize restart with new traders
	void begin_add();
	///add new trader - prepare
	/**
	 * @param id trader's
	 * @param config trader's config
	 */
	void add_trader(std::string_view id, const json::Value &config);
	///commit changes, restart cycle
	/** function can throw an exception if something fails, in this case, new traders are not
	 * started. Some currently running traders may be disabled, especially when
	 * they are going to be deleted
	 */
	void commit_add();



protected:
	PBrokerList brokers;
	PStorageFactory sf;
	PReport rpt;
	PPerfModule perfMod;
	PBalanceMap balanceCache;
	PBalanceMap extBalances;
	PHistStorageFactory<HistMinuteDataItem> hsf;


	PBalanceMap prepared_trader_conflict_map;
	PWalletDB prepared_walletDB;
	PWalletDB walletDB;

	std::mutex mx;

	using TraderList = ondra_shared::linear_map<std::string, PTrader>;

	TraderList traders, prepared;
	std::atomic<bool> stop = false;


};

using PTraders = ondra_shared::SharedObject<Traders>;


#if 0
#include "../shared/scheduler.h"
#include "../shared/shared_object.h"
#include "../shared/worker.h"
#include "istockapi.h"
#include "mtrader.h"
#include "stats2report.h"

using ondra_shared::Worker;
using ondra_shared::SharedObject;

using StatsSvc = Stats2Report;

class NamedMTrader: public MTrader {
public:
	NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, const WalletCfg &wcfg, Config cfg, std::string &&name);
	void perform(bool manually);
	const std::string ident;

};



class Traders {
public:

	using TMap = ondra_shared::linear_map<std::string_view, SharedObject<NamedMTrader> >;
	TMap traders;
    StockSelector stockSelector;
	PStorageFactory &sf;
	PReport rpt;
	PPerfModule perfMod;
	std::string iconPath;

	Traders(ondra_shared::Scheduler sch,
			const ondra_shared::IniConfig::Section &ini,
			PStorageFactory &sf,
			const PReport &rpt,
			const PPerfModule &perfMod,
			std::string iconPath,
			int brk_timeout);
	Traders(const Traders &&other) = delete;
	void clear();


	TMap::const_iterator begin() const;
	TMap::const_iterator end() const;



	void addTrader(const MTrader::Config &mcfg, ondra_shared::StrViewA n);
	void removeTrader(ondra_shared::StrViewA n, bool including_state);


	void report_util(std::string_view ident, double ms);

	template<typename Fn>
	void enumTraders(Fn &&fn) const {
		for (auto k: traders) fn(std::move(k));
	}

	void resetBrokers();
	SharedObject<NamedMTrader> find(std::string_view id) const;
	WalletCfg wcfg;


	using Utilization = std::unordered_map<std::string, std::pair<double,std::size_t> >;
	double reset_time;

	Utilization utilization;

	json::Value getUtilization(std::size_t lastUpdate) const;

	void initExternalAssets(json::Value config);

	static void resetBroker(const PStockApi &api, const std::chrono::system_clock::time_point &now);

};


#endif

#endif /* SRC_MAIN_TRADERS_H_ */
