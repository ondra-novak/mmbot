/*
 * stats2report.h
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STATS2REPORT_H_
#define SRC_MAIN_STATS2REPORT_H_

#include <memory>
#include "istatsvc.h"
#include "report.h"

using CalcSpreadFn = std::function<void()>;
using CalcSpreadQueue = std::function<void(CalcSpreadFn &&) >;


class Stats2Report: public IStatSvc {
public:

	struct SpreadInfo {
		double spread = 0;
		bool pending = false;
	};


	Stats2Report(
			CalcSpreadQueue q,
			std::string name,
			Report &rpt,
			int interval
			)
			:q(q),rpt(rpt),name(name),interval(interval)
				,spread(std::make_shared<SpreadInfo>()) {}

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.setOrders(name, buy, sell);
	}
	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades, bool margin) override {
		rpt.setTrades(name,trades,margin);
	}
	virtual void reportMisc(const MiscData &miscData) override{
		rpt.setMisc(name, miscData);
	}
	virtual void reportError(const ErrorObj &errorObj) override{
		rpt.setError(name, errorObj);
	}

	virtual void setInfo(const Info &info) override{
		rpt.setInfo(name, info);
	}
	virtual void reportPrice(double price) override{
		rpt.setPrice(name, price);
	}
	virtual std::size_t getHash() const override {
		std::hash<std::string> h;
		return h(name);
	}
	virtual void clear() override {
		rpt.clear(name);
	}

	CalcSpreadQueue q;
	Report &rpt;
	std::string name;
	int interval;
	mutable int cnt = 0;
	std::shared_ptr<SpreadInfo> spread;


};




#endif /* SRC_MAIN_STATS2REPORT_H_ */
