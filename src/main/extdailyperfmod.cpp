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
			jrep.set("pos",report.position);
			jrep.set("tradeId",report.tradeId);
			jrep.set("uid",report.uid);
			jrep.set("change",report.change);
			jrep.set("time", report.time);
			jrep.set("invert_price",report.invert_price);
			jrep.set("acbpnl", report.acb_pnl);
			jsonRequestExchange("sendItem", jrep);
			flushCache = true;

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
}

json::Value ExtDailyPerfMod::getReport() {
	if (!reportCache.defined() || flushCache) {
		try {
			reportCache = jsonRequestExchange("getReport",json::Value());
			flushCache = false;
		} catch (std::exception &e) {
			if (!reportCache.defined()) {
				reportCache = json::Object {
					{"hdr",{"error", e.what()}},
					{"rows",json::array},
					{"sums",json::array},
				};
			}
		}
	}
	return reportCache;
}
