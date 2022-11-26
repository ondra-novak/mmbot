/*
 * stats2report.h
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STATS2REPORT_H_
#define SRC_MAIN_STATS2REPORT_H_

#include <memory>

#include "../shared/shared_lockable_ptr.h"
#include "idailyperfmod.h"
#include "istatsvc.h"
#include "report.h"

using CalcSpreadFn = std::function<void()>;
using CalcSpreadQueue = std::function<void(CalcSpreadFn &&) >;


using PReport = ondra_shared::shared_lockable_ptr<Report>;
using PPerfModule = ondra_shared::shared_lockable_ptr<IDailyPerfModule>;

class Stats2Report: public IStatSvc {
public:



	Stats2Report(
			std::string name,
			const PReport &rpt,
			PPerfModule perfmod
			) :rpt(rpt),name(name),perfmod(perfmod)  {
		rev = rpt.lock_shared()->getRev();
	}

	virtual void reportOrders(int n, const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.lock()->setOrders(rev, name, n, buy, sell);
	}
	virtual void reportTrades(const IStatSvc::TradesInfo &tinfo,ondra_shared::StringView<IStatSvc::TradeRecord> trades) override {
		rpt.lock()->setTrades(rev, name,tinfo, trades);
	}
	virtual void reportMisc(const MiscData &miscData,bool initial) override{
		rpt.lock()->setMisc(rev, name, miscData, initial);
	}
	virtual void reportError(const ErrorObj &errorObj) override{
		rpt.lock()->setError(rev, name, errorObj);
	}

	virtual void setInfo(const Info &info) override{
		rpt.lock()->setInfo(rev, name, info);
	}
	virtual void reportPrice(double price) override{
		rpt.lock()->setPrice(rev, name, price);
	}
	virtual std::size_t getHash() const override {
		std::hash<std::string> h;
		return h(name);
	}
	virtual void reportPerformance(const PerformanceReport &repItem) override {
		if (perfmod) perfmod.lock()->sendItem(repItem);
	}

	PReport rpt;
	std::string name;
	PPerfModule perfmod;
	std::size_t rev;


};




#endif /* SRC_MAIN_STATS2REPORT_H_ */
