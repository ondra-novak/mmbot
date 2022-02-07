/*
 * localdailyperfmod.h
 *
 *  Created on: 25. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_LOCALDAILYPERFMOD_H_
#define SRC_MAIN_LOCALDAILYPERFMOD_H_
#include <fstream>
#include <vector>

#include <imtjson/value.h>
#include <imtjson/array.h>
#include "idailyperfmod.h"
#include "istorage.h"
#include "report.h"

class LocalDailyPerfMonitor: public IDailyPerfModule {
public:

	LocalDailyPerfMonitor(PStorage &&storage, std::string logfile, bool ignore_simulator);


	virtual void sendItem(const PerformanceReport &report) override;
	virtual json::Value getReport()  override;

	virtual bool querySupported() override {return false;}
	virtual QueryResult query(const QueryParams &param) override {return {};}
	virtual json::Value getOptions() override {return json::Value();}
	virtual void setTradeDeleted(const TradeLocation &loc, bool deleted) override {}
	virtual bool setTradeDeletedSupported() override {return false;}
	virtual json::Value getTraders() override {return json::Value();}


protected:
	PStorage storage;
	unsigned int dayIndex;
	std::ofstream logf;
	std::string logfile;
	bool ignore_simulator;
	json::Value dailySums;
	json::Value report;

	void init(unsigned int curDayIndex);
	void aggregate(unsigned int curDayIndex);
	void save();
	void prepareReport();
	void checkInit();



	static std::size_t daySeconds;


};



#endif /* SRC_MAIN_LOCALDAILYPERFMOD_H_ */
