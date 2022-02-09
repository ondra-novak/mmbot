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

bool ExtDailyPerfMod::querySupported() {
	try {
		jsonRequestExchange("query", json::Value());
		return true;
	} catch (const Exception &e) {
		if (e.isResponse()) return false;
		throw;
	}
}

ExtDailyPerfMod::QueryResult ExtDailyPerfMod::query(const QueryParams &param) {

	json::Object flt;
	flt.set("cursor", param.cursor);
	if (param.year) flt.set("year",param.year);
	if (param.month) flt.set("month",param.month);
	flt.set("start",param.start_date);
	if (param.end_date) flt.set("end",param.end_date);
	if (!param.asset.empty()) flt.set("asset", param.asset);
	if (!param.currency.empty()) flt.set("currency", param.currency);
	if (!param.broker.empty()) flt.set("broker", param.broker);
	if (param.uid.has_value()) flt.set("uid", *param.uid);
	if (param.magic.has_value()) flt.set("magic", *param.magic);
	if (param.magic.has_value()) flt.set("magic", *param.magic);
	flt.set("aggregate", param.aggregate);
	flt.set("skip_deleted", param.skip_deleted);
	flt.set("limit", param.limit);

	json::Value out = jsonRequestExchange("query", flt);
	return {
		out["complete"].getBool(),
		out["cursor"].getUIntLong(),
		out["result"]
	};



}

json::Value ExtDailyPerfMod::getOptions() {
	return jsonRequestExchange("options", json::Value());
}

void ExtDailyPerfMod::setTradeDeleted(const TradeLocation &loc, bool deleted) {
	jsonRequestExchange("deleted", json::Object{
		{"id",{loc.cursor, loc.time, loc.uid, loc.magic}},
		{"deleted", deleted}
	});
}

bool ExtDailyPerfMod::setTradeDeletedSupported() {
	try {
		jsonRequestExchange("deleted", json::Value());
		return true;
	} catch (const Exception &e) {
		if (e.isResponse()) return false;
		throw;
	}
}

json::Value ExtDailyPerfMod::getTraders() {
	return jsonRequestExchange("traders", json::Value());
}
