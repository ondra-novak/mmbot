/*
 * trades.h
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADERS_H_
#define SRC_MAIN_TRADERS_H_
#include "../shared/scheduler.h"
#include "istockapi.h"
#include "mtrader.h"
#include "stats2report.h"


using StatsSvc = Stats2Report;

class NamedMTrader: public MTrader {
public:
	NamedMTrader(IStockSelector &sel, StoragePtr &&storage, PStatSvc statsvc, Config cfg, std::string &&name);
	void perform(bool manually);
	const std::string ident;

};

class StockSelector: public IStockSelector{
public:
	using PStockApi = std::unique_ptr<IStockApi>;
	using StockMarketMap =  ondra_shared::linear_map<std::string, PStockApi, std::less<>>;

	StockMarketMap stock_markets;

	void loadStockMarkets(const ondra_shared::IniConfig::Section &ini, bool test);
	virtual IStockApi *getStock(const std::string_view &stockName) const override;
//	void addStockMarket(ondra_shared::StrViewA name, PStockApi &&market);
	virtual void forEachStock(EnumFn fn)  const override;
	void clear();
};


class Traders {
public:

	using TMap = ondra_shared::linear_map<json::StrViewA,std::unique_ptr<NamedMTrader> >;
	TMap traders;
    StockSelector stockSelector;
	bool test;
	PStorageFactory &sf;
	Report &rpt;
	IDailyPerfModule &perfMod;
	std::string iconPath;

	Traders(ondra_shared::Scheduler sch,
			const ondra_shared::IniConfig::Section &ini,
			bool test,
			PStorageFactory &sf,
			Report &rpt,
			IDailyPerfModule &perfMod,
			std::string iconPath);
	Traders(const Traders &&other) = delete;
	void clear();

	TMap::const_iterator begin() const;
	TMap::const_iterator end() const;



	void addTrader(const MTrader::Config &mcfg, ondra_shared::StrViewA n);
	void removeTrader(ondra_shared::StrViewA n, bool including_state);
	void loadIcons(const std::string &path);


	void runTraders(bool manually);
	void resetBrokers();
	NamedMTrader *find(json::StrViewA id) const;

private:
	void loadIcon(MTrader &t);
};




#endif /* SRC_MAIN_TRADERS_H_ */
