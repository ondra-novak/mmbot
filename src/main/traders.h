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
	bool perform();
	const std::string ident;

};

class StockSelector: public IStockSelector{
public:
	using PStockApi = std::unique_ptr<IStockApi>;
	using StockMarketMap =  ondra_shared::linear_map<std::string, PStockApi, std::less<>>;

	StockMarketMap stock_markets;

	void loadStockMarkets(const ondra_shared::IniConfig::Section &ini, bool test);
	virtual IStockApi *getStock(const std::string_view &stockName) const override;
	void addStockMarket(ondra_shared::StrViewA name, PStockApi &&market);
	virtual void forEachStock(EnumFn fn)  const override;
	void clear();
};

class ActionQueue: public ondra_shared::RefCntObj {
public:
ActionQueue(const ondra_shared::Scheduler &sch):sch(sch) {}

template<typename Fn>
void push(Fn &&fn) {
	bool e = dsp.empty();
	std::move(fn) >> dsp;
	if (e) goon();
}

void exec() {
	if (!dsp.empty()) {
		dsp.pump();
		goon();
	}
}

void goon() {
	sch.after(std::chrono::seconds(1)) >> [me = ondra_shared::RefCntPtr<ActionQueue>(this)]{
			me->exec();
	};
}

protected:
ondra_shared::Dispatcher dsp;
ondra_shared::Scheduler sch;
};


class Traders {
public:

	using TMap = ondra_shared::linear_map<json::StrViewA,std::unique_ptr<NamedMTrader> >;
	TMap traders;
    StockSelector stockSelector;
	ondra_shared::RefCntPtr<ActionQueue> aq;
	bool test;
	int spread_calc_interval;
	StorageFactory &sf;
	Report &rpt;

	Traders(ondra_shared::Scheduler sch,
			const ondra_shared::IniConfig::Section &ini,
			bool test,
			int spread_calc_interval,
			StorageFactory &sf,
			Report &rpt);
	Traders(const Traders &&other) = delete;
	void clear();

	TMap::const_iterator begin() const;
	TMap::const_iterator end() const;



	void addTrader(const MTrader::Config &mcfg, ondra_shared::StrViewA n);
	void removeTrader(ondra_shared::StrViewA n, bool including_state);

	void loadTraders(const ondra_shared::IniConfig &ini, ondra_shared::StrViewA names);

	bool runTraders();
	NamedMTrader *find(json::StrViewA id) const;
};




#endif /* SRC_MAIN_TRADERS_H_ */
