/*
 * extdailyperfmod.cpp
 *
 *  Created on: 26. 10. 2019
 *      Author: ondra
 */

#include "extdailyperfmod.h"

using json::Object;

#include "imtjson/object.h"

std::size_t ExtDailyPerfMod::daySeconds = 86400;

void ExtDailyPerfMod::sendItem(const PerformanceReport &report) {


	Object jrep;
	jrep.set("broker",report.broker);
	jrep.set("currency",report.currency);
	jrep.set("magic",report.magic);
	jrep.set("price",report.price);
	jrep.set("size",report.size);
	jrep.set("tradeId",report.tradeId);
	jrep.set("uid",report.uid);
	jsonRequestExchange("sendItem", jrep);


}

json::Value ExtDailyPerfMod::getReport() {
	std::size_t newidx = time(nullptr)/daySeconds;
	if (dayIndex != newidx) {
		reportCache = jsonRequestExchange("getReport", json::Value());
		dayIndex = newidx;
	}
	return reportCache;
}
