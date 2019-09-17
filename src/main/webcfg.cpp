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
#include "../imtjson/src/imtjson/binary.h"
#include "../imtjson/src/imtjson/ivalue.h"
#include "../imtjson/src/imtjson/parser.h"
#include "../imtjson/src/imtjson/operations.h"
#include "../server/src/simpleServer/query_parser.h"
#include "../server/src/simpleServer/urlencode.h"
#include "../shared/ini_config.h"

using namespace json;
using ondra_shared::StrViewA;
using ondra_shared::IniConfig;
using namespace simpleServer;

NamedEnum<WebCfg::Command> WebCfg::strCommand({
	{WebCfg::config, "config"},
	{WebCfg::restart, "restart"},
	{WebCfg::serialnr, "serial"},
	{WebCfg::brokers, "brokers"}
});

WebCfg::WebCfg(
		ondra_shared::RefCntPtr<State> state,
		const std::string &realm,
		Traders &traders,
		Dispatch &&dispatch)
	:auth(realm, state->admins)
	,traders(traders)
	,dispatch(std::move(dispatch))
	,state(state)
{

}

WebCfg::~WebCfg() {
}

bool WebCfg::operator ()(const simpleServer::HTTPRequest &req,
		const ondra_shared::StrViewA &vpath) const {

	QueryParser qp(vpath);
	StrViewA path = qp.getPath();
	auto splt = path.split("/",3);
	splt();
	StrViewA pfx = splt();
	if (pfx != "api") return false;
	StrViewA c = splt();
	StrViewA rest = splt();
	auto cmd = strCommand.find(c);
	if (cmd == nullptr) {
		return false;
	} else {
		if (!auth.checkAuth(req)) return true;
		switch (*cmd) {
		case config: return reqConfig(req);
		case restart: return reqRestart(req);
		case serialnr: return reqSerial(req);
		case brokers: return reqBrokers(req, rest);
		}
	}
	return false;
}

static Value hashPswds(Value data, StrViewA section) {
	Value list = data[section];
	Object o;
	for (Value v: list) {
		if (v.type() ==json::array) {
			o.set(v.getKey(), AuthUserList::hashPwd(v.getKey(),v[0].getString()));
		} else {
			o.set(v);
		}
	}
	return list.replace(section, o);
}

bool WebCfg::reqConfig(simpleServer::HTTPRequest req) const {

	if (!req.allowMethods({"GET","PUT"})) return true;
	if (req.getMethod() == "GET") {

		Sync _(state->lock);
		json::Value data = state->config->load();
		if (!data.defined()) data = Object("revision",0);
		req.sendResponse("application/json",data.stringify());

	} else {

		state->lock.lock();
		Traders &traders = this->traders;
		RefCntPtr<State> state = this->state;
		req.readBodyAsync(1024*1024, [state,&traders](simpleServer::HTTPRequest req) mutable {
			Sync _(state->lock);
			state->lock.unlock();

			Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
			unsigned int serial = data["revision"].getUInt();
			if (serial != state->write_serial) {
				req.sendErrorPage(409);
				return ;
			}
			data = hashPswds(data, "users");
			data = hashPswds(data, "admins");
			Value trs = data["traders"];
			for (Value v: trs) {
				StrViewA name = v.getKey();
				try {
					MTrader::load(v,false);
				} catch (std::exception &e) {
					std::string msg(name.data,name.length);
					msg.append(" - ");
					msg.append(e.what());
					req.sendErrorPage(406, StrViewA(), msg);
					return;
				}
			}
			data = data.replace("revision", state->write_serial+1);
			state->config->store(data);
			state->write_serial = serial+1;;
			req.sendErrorPage(202);
			state->applyConfig(traders);

		});


	}
	return true;


}

bool WebCfg::reqRestart(simpleServer::HTTPRequest ) const {
	return false;
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


bool WebCfg::reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) const {
	if (rest.empty()) {
		if (!req.allowMethods({"GET"})) return true;
		Array brokers;
		traders.stockSelector.forEachStock([&](const std::string_view &name,IStockApi &){
			brokers.push_back(name);
		});
		Object obj("entries", brokers);
		req.sendResponse("application/json",Value(obj).stringify());
	} else {
		json::String vpath = rest;
		auto splt = StrViewA(vpath).split("/");
		StrViewA urlbroker = splt();
		std::string broker = urlDecode(urlbroker);
		IStockApi *api = traders.stockSelector.getStock(broker);
		if (api == nullptr) {
			req.sendErrorPage(404);
			return true;
		}

		dispatch([vpath,req, this, api]() mutable {

			try {
				auto splt = StrViewA(vpath).split("/");
				splt();
				StrViewA entry = splt();
				StrViewA pair = splt();
				StrViewA orders = splt();

				api->reset();

				if (entry.empty()) {
					if (!req.allowMethods({"GET"})) return true;
					auto binfo  = api->getBrokerInfo();
					Value res = Object("name", binfo.name)
									("exchangeName", binfo.exchangeName)
									("exchangeUrl", binfo.exchangeUrl)
									("version", binfo.version)
									("licence", binfo.licence)
									("entries", {"icon.png","pairs"});
					req.sendResponse("application/json",res.stringify());
					return true;
				} else if (entry == "icon.png") {
					if (!req.allowMethods({"GET"})) return true;
					auto binfo  = api->getBrokerInfo();
					Value v = base64->decodeBinaryValue(binfo.favicon);
					Binary b = v.getBinary(base64);
					req.sendResponse(HTTPResponse(200).contentType("image/png").cacheFor(600),
							StrViewA(b));
					return true;
				} else if (entry == "pairs"){
					if (pair.empty()) {
						if (!req.allowMethods({"GET"})) return true;
						Array p;
						auto pairs = api->getAllPairs();
						for (auto &&k: pairs) p.push_back(k);
						Object obj("entries", p);
						req.sendResponse("application/json",Value(obj).stringify());
						return true;
					} else {
						std::string p = urlDecode(pair);

						try {
							if (orders.empty()) {
								if (!req.allowMethods({"GET"})) return true;
								IStockApi::MarketInfo nfo = api->getMarketInfo(p);
								double ab = getSafeBalance(api,nfo.asset_symbol);
								double cb = getSafeBalance(api,nfo.currency_symbol);
								Value resp = Object
											("asset_symbol",nfo.asset_symbol)
											("currency_symbol", nfo.currency_symbol)
											("fees", nfo.fees)
											("leverage", nfo.leverage)
											("invert_price", nfo.invert_price)
											("asset_balance", ab)
											("currency_balance", cb)
											("price", api->getTicker(pair).last)
											("entries", {"orders","ticker"});
								req.sendResponse("application/json",resp.stringify());
								return true;
							} else if (orders == "ticker") {
								if (!req.allowMethods({"GET"})) return true;
								auto t = api->getTicker(p);
								Value ticker = Object
										("ask", t.ask)
										("bid", t.bid)
										("last", t.last)
										("time", t.time);
								req.sendResponse("application/json",ticker.stringify());
								return true;
							} else if (orders == "orders") {
								if (!req.allowMethods({"GET","POST"})) return true;
								if (req.getMethod() == "GET") {
									auto ords = api->getOpenOrders(p);
									Value orders = Value(json::array,ords.begin(), ords.end(), [&](const IStockApi::Order &ord) {
										return Object
												("price",ord.price)
												("size", ord.size)
												("clientId",ord.client_id)
												("id",ord.id);
									});
									req.sendResponse("application/json",orders.stringify());
									return true;
								} else {
									Stream s = req.getBodyStream();
									Value parsed = Value::parse(s);
									Value res = api->placeOrder(pair,
											parsed["size"].getNumber(),
											parsed["price"].getNumber(),
											parsed["clientId"],
											parsed["replaceId"],
											parsed["replaceSize"].getNumber());
									req.sendResponse("application/json",res.stringify());
									return true;
								}

							}

						} catch (...) {
							auto pp = api->getAllPairs();
							auto f = std::find(pp.begin(), pp.end(), p);
							if (f==pp.end()) {
								req.sendErrorPage(404);
								return true;
							} else {
								throw;
							}

						}
					}
				}
				req.sendErrorPage(404);
				return true;
			} catch (std::exception &e) {
				req.sendErrorPage(500, StrViewA(), e.what());
				return true;
			}
		});
	}
	return true;

}

static void AULFromJSON(json::Value js, AuthUserList &aul) {
	using UserVector = std::vector<AuthUserList::LoginPwd>;
	using LoginPwd = AuthUserList::LoginPwd;

	UserVector ulist = js.reduce([&](
			UserVector &curVal, Value r){

		curVal.push_back(LoginPwd(r.getKey(), r.getString()));
		return curVal;
	},UserVector());

	aul.setUsers(std::move(ulist));
}


void WebCfg::State::init(json::Value data) {
	if (data.defined()) {
		this->write_serial = data["revision"].getUInt();
		AULFromJSON(data["users"],*users);
		AULFromJSON(data["admins"],*admins);
	}

}
void WebCfg::State::applyConfig(Traders &t) {
	auto data = config->load();
	init(data);
	for (auto &&n :traderNames) {
		t.removeTrader(n);
	}

	for (Value v: data["traders"]) {
		t.addTrader(MTrader::load(v, t.test),v.getKey());
		traderNames.push_back(v.getKey());
	}
}

void WebCfg::State::init() {
	init(config->load());
}

