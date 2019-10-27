/*
 * extdailyperfmod.cpp
 *
 *  Created on: 26. 10. 2019
 *      Author: ondra
 */

#include "extdailyperfmod.h"

#include "../shared/logOutput.h"
using json::Object;
using json::Value;
using ondra_shared::logError;

#include "imtjson/object.h"

std::size_t ExtDailyPerfMod::daySeconds = 86400;

void ExtDailyPerfMod::sendItem(const PerformanceReport &report) {


	try {

		Object jrep;
		jrep.set("broker",report.broker);
		jrep.set("currency",report.currency);
		jrep.set("magic",report.magic);
		jrep.set("price",report.price);
		jrep.set("size",report.size);
		jrep.set("tradeId",report.tradeId);
		jrep.set("uid",report.uid);
		jsonRequestExchange("sendItem", jrep);

	} catch (std::exception &e) {
		logError("ExtDailyPerfMod: $1", e.what());
	}
}

json::Value ExtDailyPerfMod::getReport() {
	std::size_t newidx = time(nullptr)/daySeconds;
	if (dayIndex != newidx) {
		try {
			reportCache = jsonRequestExchange("getReport", json::Value());
			dayIndex = newidx;
		} catch (std::exception &e) {
			return Object("hdr",Value(json::array, {"error"}))
						("rows",Value(json::array, {Value(json::array,{e.what()})}));
		}
	}
	return reportCache;
}
