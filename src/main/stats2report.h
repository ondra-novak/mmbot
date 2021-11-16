/*
 * stats2report.h
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STATS2REPORT_H_
#define SRC_MAIN_STATS2REPORT_H_

#include <memory>

#include "../shared/shared_object.h"
#include "idailyperfmod.h"
#include "istatsvc.h"
#include "report.h"

using CalcSpreadFn = std::function<void()>;
using CalcSpreadQueue = std::function<void(CalcSpreadFn &&) >;


using PReport = ondra_shared::SharedObject<Report>;
using PPerfModule = ondra_shared::SharedObject<IDailyPerfModule, ondra_shared::SharedObjectVirtualTraits<IDailyPerfModule> >;

class Stats2Report: public IStatSvc {
public:



	Stats2Report(
			std::string name,
			const PReport &rpt,
			PPerfModule perfmod
			) :rpt(rpt),name(name),perfmod(perfmod)  {}

	virtual void reportOrders(int n, const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.lock()->setOrders(name, n, buy, sell);
	}
	virtual void reportTrades(double finalPos, ondra_shared::StringView<IStatSvc::TradeRecord> trades) override {
		rpt.lock()->setTrades(name,finalPos, trades);
	}
	virtual void reportMisc(const MiscData &miscData) override{
		rpt.lock()->setMisc(name, miscData);
	}
	virtual void reportError(const ErrorObj &errorObj) override{
		rpt.lock()->setError(name, errorObj);
	}

	virtual void setInfo(const Info &info) override{
		rpt.lock()->setInfo(name, info);
	}
	virtual void reportPrice(double price) override{
		rpt.lock()->setPrice(name, price);
	}
	virtual std::size_t getHash() const override {
		std::hash<std::string> h;
		return h(name);
	}
	virtual void clear() override {
		rpt.lock()->clear(name);
	}
	virtual void reportPerformance(const PerformanceReport &repItem) override {
		if (perfmod) perfmod.lock()->sendItem(repItem);
	}

	PReport rpt;
	std::string name;
	PPerfModule perfmod;


};




#endif /* SRC_MAIN_STATS2REPORT_H_ */
