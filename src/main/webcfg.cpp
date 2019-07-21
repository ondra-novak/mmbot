/*
 * webcfg.cpp
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */


#include "webcfg.h"

#include <imtjson/array.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../server/src/simpleServer/query_parser.h"
#include "../shared/ini_config.h"

using namespace json;
using ondra_shared::StrViewA;
using ondra_shared::IniConfig;
using namespace simpleServer;

NamedEnum<WebCfg::Command> WebCfg::strCommand({
	{WebCfg::all_pairs, "/all_pairs"},
	{WebCfg::config, "/config"},
	{WebCfg::restart, "/restart"},
	{WebCfg::serialnr, "/serial"},
	{WebCfg::info, "/info"}
});

WebCfg::WebCfg(const std::string &auth,
		const std::string &realm,
		const std::string &config_path,
		unsigned int serial,
		IStockSelector &stockSelector, std::function<void()> &&restart_fn)
	:auth(auth, realm)
	,config_path(config_path)
	,stockSelector(stockSelector)
	,restart_fn(std::move(restart_fn))
	,serial(serial)
{
}

bool WebCfg::operator ()(const simpleServer::HTTPRequest &req,
		const ondra_shared::StrViewA &vpath) const {

	QueryParser qp(vpath);
	auto cmd = strCommand.find(qp.getPath());
	if (cmd == nullptr) {
		return false;
	} else {
		if (!auth.checkAuth(req)) return true;
		switch (*cmd) {
		case all_pairs: return reqAllPairs(req);
		case config: return reqConfig(req);
		case restart: return reqRestart(req);
		case serialnr: return reqSerial(req);
		case info: return reqInfo(req,qp["broker"],qp["symbol"]);
		}
	}
	return false;
}

bool WebCfg::reqAllPairs(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"GET"})) return true;
	Object stocks;
	stockSelector.forEachStock([&](const std::string_view &name,IStockApi &stock){
		Array markets;
		auto m = stock.getAllPairs();
		for (auto &&k:m) markets.push_back(k);
		stocks.set(name, markets);
	});
	req.sendResponse("application/json",Value(stocks).stringify());
	return true;
}

bool WebCfg::reqConfig(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"GET","PUT"})) return true;
	if (req.getMethod() == "GET") {

		IniConfig cfg;
		cfg.load(config_path);
		Object sections;
		for (auto &&k: cfg) {
			Object values;
			for (auto &&v: k.second) {
				values.set(v.first, v.second.getString());
			}
			sections.set(k.first, values);
		}
		req.sendResponse("application/json",Value(sections).stringify());

	} else {
		req.sendErrorPage(503);
	}
	return true;


}

bool WebCfg::reqRestart(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST"})) return true;
	restart_fn();
	req.sendErrorPage(201);
	return true;
}

bool WebCfg::reqSerial(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"GET"})) return true;
	req.sendResponse("text/plain") << serial << "\r\n";
	return true;
}


static double getSafeBalance(IStockApi *api, std::string_view symb) {
	try {
		return api->getBalance(symb);
	} catch (...) {
		return 0;
	}
}

bool WebCfg::reqInfo(simpleServer::HTTPRequest req, ondra_shared::StrViewA broker, ondra_shared::StrViewA symbol) const {
	if (!req.allowMethods({"GET"})) return true;
	IStockApi *api = stockSelector.getStock(broker);
	if (api == nullptr) return false;
	try {
		IStockApi::MarketInfo nfo = api->getMarketInfo(symbol);
		double ab = getSafeBalance(api,nfo.asset_symbol);
		double cb = getSafeBalance(api,nfo.currency_symbol);
		Value resp = Object
					("asset_symbol",nfo.asset_symbol)
					("currency_symbol", nfo.currency_symbol)
					("fees", nfo.fees)
					("leverage", nfo.leverage)
					("asset_balance", ab)
					("currency_balance", cb)
					("price", api->getTicker(symbol).last);
		req.sendResponse("application/json",resp.stringify());
	} catch (...) {
		return false;
	}
	return true;


}
