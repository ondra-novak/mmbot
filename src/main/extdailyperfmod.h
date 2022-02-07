/*
 * extdailyperfmod.h
 *
 *  Created on: 26. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EXTDAILYPERFMOD_H_
#define SRC_MAIN_EXTDAILYPERFMOD_H_

#include "abstractExtern.h"
#include "idailyperfmod.h"

class ExtDailyPerfMod: public IDailyPerfModule, public AbstractExtern {
public:
	ExtDailyPerfMod(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline, bool ignore_simulator, int timeout);

	virtual void sendItem(const PerformanceReport &report) override;
	virtual json::Value getReport() override;
	virtual bool querySupported() override;
	virtual QueryResult query(const QueryParams &param) override;
	virtual json::Value getOptions() override;
	virtual void setTradeDeleted(const TradeLocation &loc, bool deleted) override;
	virtual bool setTradeDeletedSupported() override;
	virtual json::Value getTraders() override;

public:
	bool ignore_simulator;
	json::Value reportCache;
	bool flushCache = true;


};

#endif /* SRC_MAIN_EXTDAILYPERFMOD_H_ */
