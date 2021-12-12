/*
 * extdailyperfmod.cpp
 *
 *  Created on: 26. 10. 2019
 *      Author: ondra
 */

#include "extdailyperfmod.h"

#include <random>
#include "../shared/logOutput.h"
using json::Object;
using json::Value;
using ondra_shared::logError;

#include "imtjson/object.h"

std::size_t ExtDailyPerfMod::daySeconds = 86400;

void ExtDailyPerfMod::sendItem(const PerformanceReport &report) {

	if (!report.simulator || ignore_simulator) {

		try {

			Object jrep;
			jrep.set("broker",report.broker);
			jrep.set("currency",report.currency);
			jrep.set("asset",report.asset);
			jrep.set("magic",report.magic);
			jrep.set("price",report.price);
			jrep.set("size",report.size);
			jrep.set("tradeId",report.tradeId);
			jrep.set("uid",report.uid);
			jrep.set("change",report.change);
			jrep.set("time", report.time);
			jrep.set("invert_price",report.invert_price);
			jrep.set("acbpnl", report.acb_pnl);
			jsonRequestExchange("sendItem", jrep);

		} catch (std::exception &e) {
			logError("ExtDailyPerfMod: $1", e.what());
		}
	}
}

ExtDailyPerfMod::ExtDailyPerfMod(const std::string_view &workingDir,
		const std::string_view &name, const std::string_view &cmdline,
		bool ignore_simulator, int timeout)
:AbstractExtern(workingDir, name, cmdline, timeout),ignore_simulator(ignore_simulator)
{
	std::random_device r;
	rnd = r() % 1000; //avoid multiple robots requests the database at same time. This allows
					//to request regenerate report randomly in 1000 seconds
}

json::Value ExtDailyPerfMod::getReport() {
	time_t now = time(nullptr);
	std::size_t newidx = (now-rnd)/(daySeconds/12);
	std::size_t repDayIndex = now/daySeconds;
	if (dayIndex != newidx) {
		try {
			reportCache = jsonRequestExchange("getReport", json::Value(repDayIndex));
			dayIndex = newidx;
		} catch (std::exception &e) {
			if (reportCache.hasValue()) return reportCache;
			else return Object({{"hdr",Value(json::array, {"error"})},
				{"rows",Value(json::array, {Value(json::array,{e.what()})})}});
		}
	}
	return reportCache;
}
