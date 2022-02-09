/*
 * rptapi.cpp
 *
 *  Created on: 7. 2. 2022
 *      Author: ondra
 */

#include <imtjson/string.h>
#include <imtjson/object.h>
#include <imtjson/serializer.h>
#include <imtjson/operations.h>
#include "rptapi.h"

#include "../brokers/isotime.h"
#include "abstractExtern.h"
#include "simpleServer/query_parser.h"
RptApi::RptApi(PPerfModule perfMod):perfMod(perfMod) {
}


json::NamedEnum<RptApi::Operation> RptApi::strOperation({
	{RptApi::Operation::query,"query"},
	{RptApi::Operation::options,"options"},
	{RptApi::Operation::deltrade,"deltrade"},
	{RptApi::Operation::report,"report"},
	{RptApi::Operation::traders,"traders"},
});


bool RptApi::handle(simpleServer::HTTPRequest req, const ondra_shared::StrViewA &vpath) {

	if (vpath == "/") return reqDirectory(req);

	simpleServer::QueryParser qp(vpath.substr(1));

	const Operation *op = strOperation.find(qp.getPath());
	if (op == nullptr) return false;
	switch (*op) {
	case Operation::query: return reqQuery(req, qp);
	case Operation::options: return reqOptions(req, qp);
	case Operation::deltrade: return reqDelTrade(req, qp);
	case Operation::report: return reqReport(req, qp);
	case Operation::traders: return reqTraders(req, qp);
	default: return false;
	}

}

static const char *ctx = "application/json";
static void sendJSON(simpleServer::HTTPRequest &req, const json::Value &v) {
	req.sendResponse(ctx,v.stringify().str());
}


static std::uint64_t parseTimeLocal(const json::String &date) {
	if (date.empty()) return 0;
	return parseTime(date, ParseTimeFormat::iso);
}


bool RptApi::reqQuery(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q) {
	IDailyPerfModule::QueryParams flt = {};
	bool raw = q["raw"].getInt() != 0;
	flt.asset = q["asset"];
	flt.currency = q["currency"];
	flt.broker = q["broker"];
	flt.cursor = 0;
	flt.year = q["year"].getUInt();
	flt.month = q["month"].getUInt();
	flt.skip_deleted = q["skipdel"].getInt() != 0;
	flt.start_date = parseTimeLocal(std::string(q["start_date"]));
	flt.end_date = parseTimeLocal(std::string(q["end_date"]));
	flt.aggregate = q["aggregate"].getInt()!=0;
	if (flt.start_date == 0 && flt.year == 0) {
		req.sendErrorPage(400,"","start_date or year missing");
		return true;
	}
	std::string_view trader = q["trader"];
	if (!trader.empty()) {
		unsigned long long uid, magic;
		if (sscanf(trader.data(),"%llu-%llu", &uid, &magic) == 2) {
			flt.uid = uid;
			flt.magic = magic;
		}
	}
	flt.limit = 1000;

	auto s = req->sendResponse(ctx);
	StrViewA sep = "[\n";
	bool anyoutput = false;
	bool cont = true;
	while (cont) {
		IDailyPerfModule::QueryResult res = perfMod.lock()->query(flt);
		for (json::Value x: res.result) {
			s << sep;
			if (!anyoutput) {
				anyoutput = true;
				sep = ",\n";
			}
			json::Value out = (raw || flt.aggregate)?x:json::Value(json::Object {
					{"uid",x[0].toString()},
					{"tim",x[1].toString()},
					{"trd", json::String({x[2].toString(),"-",x[3].toString()})},
					{"ast",x[4]},
					{"cur",x[5]},
					{"brk",x[6]},
					{"tid",x[7]},
					{"prc",x[8]},
					{"tsz",x[9]},
					{"pos",x[10]},
					{"chg",x[11]},
					{"rpnl",x[12]},
					{"inv",x[13]},
					{"del",x[14]},
				});

			out.serialize(s);
		}
		cont = !res.complete;
		flt.cursor = res.cursor;
	}
	if (!anyoutput) s<<"[]";
	else s<<"]";

	return true;
}

bool RptApi::reqOptions(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q) {
	sendJSON(req, perfMod.lock()->getOptions().map([&](json::Value x){
		if (x.getKey() == "traders")
			return json::Value(x.getKey(),x.map([](json::Value y){
				return json::Value(json::String({y[0].toString(),"-",y[1].toString()}));
		}));
		return x;
	}));
	return true;
}

bool RptApi::reqDelTrade(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q) {
	if (req.allowMethods({"POST"})) {
			auto cursor = std::strtoull(q["uid"].data,0,10);
			auto time = std::strtoull(q["tim"].data,0,10);
			auto trader = q["trd"];
			unsigned long long uid, magic;
			if (sscanf(trader.data,"%llu-%llu", &uid, &magic) != 2) {
				req.sendErrorPage(400,"","Invalid trader's ID");
				return true;
			}
			auto del = q["del"].getInt() != 0;
			try {
				perfMod.lock()->setTradeDeleted({
					cursor, time, uid, magic
				}, del);
				req.sendResponse(ctx, "\"OK\"\r\n", 202);
			} catch (const AbstractExtern::Exception &e) {
				if (e.isResponse()) {
					req.sendErrorPage(400,"",e.getMsg());
				} else {
					throw;
				}
			}
	}
	return true;
}

bool RptApi::reqDirectory(simpleServer::HTTPRequest req) {
	bool extsupp = perfMod.lock()->querySupported();
	bool delsupp = perfMod.lock()->setTradeDeletedSupported();
	json::Array entry;
	for (const auto x: strOperation) {
		switch (x.val) {
		case Operation::deltrade: if (!delsupp) continue;break;
		case Operation::query:
		case Operation::traders:
		case Operation::options: if (!extsupp) continue;break;
		default:break;
		}
		entry.push_back(x.name);
	}
	sendJSON(req, json::Object {
		{"entries",entry}
	});
	return true;
}

bool RptApi::reqReport(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q) {
	sendJSON(req, perfMod.lock()->getReport());
	return true;
}

bool RptApi::reqTraders(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q) {
	sendJSON(req, perfMod.lock()->getTraders());
	return true;
}
