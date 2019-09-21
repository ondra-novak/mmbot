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
	{WebCfg::serialnr, "serial"},
	{WebCfg::brokers, "brokers"},
	{WebCfg::traders, "traders"}
});

WebCfg::WebCfg(
		ondra_shared::RefCntPtr<State> state,
		const std::string &realm,
		Traders &traders,
		Dispatch &&dispatch)
	:auth(realm, state->admins)
	,trlist(traders)
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
		case serialnr: return reqSerial(req);
		case brokers: return reqBrokers(req, rest);
		case traders: return reqTraders(req, rest);
		}
	}
	return false;
}

static Value hashPswds(Value data) {
	Value list = data["users"];
	Array o;
	for (Value v: list) {
		Value p = v["password"];
		if (p.defined()) {
			Value u = v["username"];
			Value pwdhash = AuthUserList::hashPwd(u.toString().str(),p.toString().str());
			v = v.merge(Value(json::object,{
					Value(p.getKey(), json::undefined),
					Value("pwdhash", pwdhash)
			},false));
		}
		o.push_back(v);
	}
	return data.replace("users", o);
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
		Traders &traders = this->trlist;
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
			data = hashPswds(data);
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
			req.sendResponse(HTTPResponse(202).contentType("application/json"),data.stringify());
			state->applyConfig(traders);

		});


	}
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


bool WebCfg::reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) const {
	if (rest.empty()) {
		if (!req.allowMethods({"GET"})) return true;
		Array brokers;
		trlist.stockSelector.forEachStock([&](const std::string_view &name,IStockApi &){
			brokers.push_back(name);
		});
		Object obj("entries", brokers);
		req.sendResponse("application/json",Value(obj).stringify());
	} else {
		json::String vpath = rest;
		auto splt = StrViewA(vpath).split("/");
		StrViewA urlbroker = splt();
		std::string broker = urlDecode(urlbroker);
		IStockApi *api = trlist.stockSelector.getStock(broker);
		if (api == nullptr) {
			req.sendErrorPage(404);
			return true;
		}


		dispatch([vpath,req, this, api]() mutable {
			HTTPResponse hdr(200);
			hdr.cacheFor(30);
			hdr.contentType("application/json");

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
									("trading_enabled", binfo.trading_enabled)
									("exchangeName", binfo.exchangeName)
									("exchangeUrl", binfo.exchangeUrl)
									("version", binfo.version)
									("licence", binfo.licence)
									("entries", {"icon.png","pairs"});
					req.sendResponse(HTTPResponse(200)
									.contentType("application/json")
									.cacheFor(30),res.stringify());
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
						req.sendResponse(std::move(hdr),Value(obj).stringify());
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
								req.sendResponse(std::move(hdr),resp.stringify());
								return true;
							} else if (orders == "ticker") {
								if (!req.allowMethods({"GET"})) return true;
								auto t = api->getTicker(p);
								Value ticker = Object
										("ask", t.ask)
										("bid", t.bid)
										("last", t.last)
										("time", t.time);
								req.sendResponse(std::move(hdr),ticker.stringify());
								return true;
							} else if (orders == "orders") {
								if (!req.allowMethods({"GET","POST","DELETE"})) return true;
								if (req.getMethod() == "GET") {
									auto ords = api->getOpenOrders(p);
									Value orders = Value(json::array,ords.begin(), ords.end(), [&](const IStockApi::Order &ord) {
										return Object
												("price",ord.price)
												("size", ord.size)
												("clientId",ord.client_id)
												("id",ord.id);
									});
									req.sendResponse(std::move(hdr),orders.stringify());
									return true;
								} else if (req.getMethod() == "DELETE") {
									auto ords = api->getOpenOrders(p);
									for (auto &&x : ords) {
										api->placeOrder(p,0,0,Value(),x.id, 0);
									}
									req.sendResponse(std::move(hdr),"true");
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
									req.sendResponse(std::move(hdr),res.stringify());
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

bool WebCfg::reqTraders(simpleServer::HTTPRequest req, ondra_shared::StrViewA vpath) const {
	std::string path = vpath;
	dispatch([path,req, this]() mutable {
		HTTPResponse hdr(200);

		try {
			if (path.empty()) {
				if (!req.allowMethods({"GET"})) return ;
				Value res (json::array, trlist.begin(), trlist.end(), [&](auto &&x) {
					return x.first;
				});
				req.sendResponse(std::move(hdr), res.stringify());
			} else {
				auto splt = StrViewA(path).split("/");
				std::string trid = urlDecode(StrViewA(splt()));
				auto tr = trlist.find(trid);
				if (tr == nullptr) {
					req.sendErrorPage(404);
				} else if (!splt) {
					if (!req.allowMethods({"GET","DELETE"})) return ;
					if (req.getMethod() == "DELETE") {
						trlist.removeTrader(trid, false);
						req.sendResponse(std::move(hdr), "true");
					} else {
						req.sendResponse(std::move(hdr),
							Value({"stop","reset","repair","trades","calculator"}).stringify());
					}
				} else {
					auto cmd = urlDecode(StrViewA(splt()));
					if (cmd == "reset") {
						if (!req.allowMethods({"POST"})) return ;
						tr->reset();
						req.sendResponse(std::move(hdr), "true");
					} else if (cmd == "stop") {
						if (!req.allowMethods({"POST"})) return ;
						tr->stop();
						req.sendResponse(std::move(hdr), "true");
					} else if (cmd == "repair") {
						if (!req.allowMethods({"POST"})) return ;
						tr->repair();
						req.sendResponse(std::move(hdr), "true");
					} else if (cmd == "trades") {
						if (!splt) {
							if (!req.allowMethods({"GET"})) return ;
							auto trades = tr->getTrades();
							req.sendResponse(std::move(hdr),
									Value(json::array, trades.begin(), trades.end(),
										[&](auto &&t) {
											return t.toJSON();
										}).stringify());
						} else {
							if (!req.allowMethods({"DELETE"})) return ;
							auto id = urlDecode(StrViewA(splt()));
							if (!tr->eraseTrade(id, false)) {
								req.sendErrorPage(404);
							} else {
								req.sendResponse(std::move(hdr), "true");
							}

						}
					} else if (cmd == "calculator") {
						if (!req.allowMethods({"GET","PUT"})) return ;
						if (req.getMethod() == "GET") {
							auto &calc = tr->getCalculator();
							req.sendResponse(std::move(hdr), Value(json::object,
									{Value("P", calc.getPrice()),
									Value("A", calc.getBalance())}).toString());
						} else {
							req.readBodyAsync(10000, [=](HTTPRequest req) {
								try {
									Value v = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
									auto &calc = tr->getCalculator();
									calc = Calculator(v["P"].getNumber(), v["A"].getNumber(),false);

								} catch (std::exception &e) {
									req.sendErrorPage(400,StrViewA(),e.what());
								}
							});
						}
					}
				}
			}
		} catch (std::exception &e) {
			req.sendErrorPage(500, StrViewA(), e.what());
		}


	});
	return true;
}



static void AULFromJSON(json::Value js, AuthUserList &aul, bool admin) {
	using UserVector = std::vector<AuthUserList::LoginPwd>;
	using LoginPwd = AuthUserList::LoginPwd;

	UserVector ulist = js.reduce([&](
			UserVector &curVal, Value r){
		Value username = r["username"];
		Value password = r["pwdhash"];
		Value isadmin = r["admin"];

		if (!admin || isadmin.getBool()) {
			curVal.push_back(LoginPwd(username.toString().str(), password.toString().str()));
		}
		return curVal;
	},UserVector());

	aul.setUsers(std::move(ulist));
}


void WebCfg::State::init(json::Value data) {
	if (data.defined()) {
		this->write_serial = data["revision"].getUInt();
		if (data["guest"].getBool() == false) {
				AULFromJSON(data["users"],*users, false);
		}else {
			users->setUsers({});
		}
		AULFromJSON(data["users"],*admins, true);
	}

}
void WebCfg::State::applyConfig(Traders &t) {
	auto data = config->load();
	init(data);
	for (auto &&n :traderNames) {
		t.removeTrader(n, !data["traders"][n].defined());
	}

	traderNames.clear();

	for (Value v: data["traders"]) {
		t.addTrader(MTrader::load(v, t.test),v.getKey());
		traderNames.push_back(v.getKey());
	}
}

void WebCfg::State::init() {
	init(config->load());
}

