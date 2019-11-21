/*
 * webcfg.cpp
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */


#include "webcfg.h"

#include <random>
#include <imtjson/array.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../imtjson/src/imtjson/binary.h"
#include "../imtjson/src/imtjson/ivalue.h"
#include "../imtjson/src/imtjson/parser.h"
#include "../imtjson/src/imtjson/operations.h"
#include "../imtjson/src/imtjson/serializer.h"
#include "../server/src/simpleServer/query_parser.h"
#include "../server/src/simpleServer/urlencode.h"
#include "../shared/ini_config.h"
#include "../shared/logOutput.h"
#include "apikeys.h"
#include "ext_stockapi.h"

using namespace json;
using ondra_shared::StrViewA;
using ondra_shared::IniConfig;
using ondra_shared::logError;
using namespace simpleServer;

NamedEnum<WebCfg::Command> WebCfg::strCommand({
	{WebCfg::config, "config"},
	{WebCfg::serialnr, "serial"},
	{WebCfg::brokers, "brokers"},
	{WebCfg::traders, "traders"},
	{WebCfg::stop, "stop"},
	{WebCfg::logout, "logout"},
	{WebCfg::logout_commit, "logout_commit"},
	{WebCfg::editor, "editor"}
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
		case stop: return reqStop(req);
		case traders: return reqTraders(req, rest);
		case logout: return reqLogout(req,false);
		case logout_commit: return reqLogout(req,true);
		case editor: return reqEditor(req);
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
		Traders &traders = this->trlist;
		RefCntPtr<State> state = this->state;


			state->lock.lock();
			req.readBodyAsync(1024*1024, [state,&traders,dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
				try {
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
							MTrader_Config().loadConfig(v,false);
						} catch (std::exception &e) {
							std::string msg(name.data,name.length);
							msg.append(" - ");
							msg.append(e.what());
							req.sendErrorPage(406, StrViewA(), msg);
							return;
						}
					}
					data = data.replace("revision", state->write_serial+1);
					Value apikeys = data["apikeys"];
					state->config->store(data.replace("apikeys", Value()));
					state->write_serial = serial+1;;
					dispatch([&traders, state, req, data, apikeys] {
						try {
							state->applyConfig(traders);
							if (apikeys.type() == json::object) {
								for (Value v: apikeys) {
									StrViewA broker = v.getKey();
									IStockApi *b = traders.stockSelector.getStock(broker);
									if (b) {
										IApiKey *apik = dynamic_cast<IApiKey *>(b);
										apik->setApiKey(v);
									}
								}
							}
							traders.runTraders(true);
							traders.rpt.genReport();
							req.sendResponse(HTTPResponse(202).contentType("application/json"),data.stringify());
						} catch (std::exception &e) {
							req.sendErrorPage(500,StrViewA(),e.what());
						}
					});


				} catch (std::exception &e) {
					req.sendErrorPage(500,StrViewA(),e.what());
				}

			});


	}
	return true;


}


bool WebCfg::reqSerial(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"GET"})) return true;
	req.sendResponse("text/plain") << serial << "\r\n";
	return true;
}


static double getSafeBalance(IStockApi *api, std::string_view symb,  std::string_view pair) {
	try {
		return api->getBalance(symb,pair);
	} catch (...) {
		return 0;
	}
}

static json::Value brokerToJSON(const IStockApi::BrokerInfo &binfo) {
	Value res = Object("name", binfo.name)("trading_enabled",
			binfo.trading_enabled)("exchangeName", binfo.exchangeName)(
			"exchangeUrl", binfo.exchangeUrl)("version", binfo.version);
			return res;
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
		return true;
	} else {
		json::String vpath = rest;
		auto splt = StrViewA(vpath).split("/",2);
		StrViewA urlbroker = splt();
		if (urlbroker == "_reload") {
			if (!req.allowMethods({"POST"})) return true;
			dispatch([=] {
				trlist.stockSelector.forEachStock([&](const std::string_view &,IStockApi &x){
					ExtStockApi *ex = dynamic_cast<ExtStockApi *>(&x);
					ex->stop();
				});
				req.sendResponse("application/json","true");
			});
			return true;
		} else if (urlbroker == "_all") {
			if (!req.allowMethods({"GET"})) return true;
			Array res;
			trlist.stockSelector.forEachStock([&](std::string_view, IStockApi &api) {
				res.push_back(brokerToJSON(api.getBrokerInfo()));
			});
			req.sendResponse("application/json",Value(res).stringify());
			return true;
		}
		std::string broker = urlDecode(urlbroker);
		IStockApi *api = trlist.stockSelector.getStock(broker);

		return reqBrokerSpec(req, splt, api);
	}
}

static Value getOpenOrders(IStockApi &api, const std::string_view &pair) {
	Value orders = json::array;
	try {
		auto ords = api.getOpenOrders(pair);
		orders = Value(json::array, ords.begin(),
				ords.end(),
				[&](const IStockApi::Order &ord) {
					return Object("price", ord.price)(
							"size", ord.size)("clientId",
							ord.client_id)("id", ord.id);
				});
	} catch (...) {

	}
	return orders;

}

static Value getPairInfo(IStockApi &api, const std::string_view &pair, const std::optional<double> &internalBalance = std::optional<double>()) {
	IStockApi::MarketInfo nfo = api.getMarketInfo(pair);
	double ab = internalBalance.has_value()?*internalBalance:getSafeBalance(&api, nfo.asset_symbol, pair);
	double cb = getSafeBalance(&api, nfo.currency_symbol, pair);
	Value resp = Object("symbol",pair)("asset_symbol", nfo.asset_symbol)(
			"currency_symbol", nfo.currency_symbol)("fees",
			nfo.fees)("leverage", nfo.leverage)(
			"invert_price", nfo.invert_price)(
			"asset_balance", ab)("currency_balance", cb)(
			"min_size", nfo.min_size)("price",
			api.getTicker(pair).last);
	return resp;

}

bool WebCfg::reqBrokerSpec(simpleServer::HTTPRequest req,
		ondra_shared::StrViewA vpath, IStockApi *api) const {

	if (api == nullptr) {
		req.sendErrorPage(404);
		return true;
	}

	HTTPResponse hdr(200);
	hdr.cacheFor(30);
	hdr.contentType("application/json");

	try {
		auto splt = StrViewA(vpath).split("/");
		StrViewA entry = splt();
		StrViewA pair = splt();
		StrViewA orders = splt();

		if (req.getPath().indexOf("reset=1") != StrViewA::npos) {
			api->reset();
		}

		if (entry.empty()) {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			Value res = brokerToJSON(binfo).replace("entries", { "icon.png", "pairs","apikey","settings","licence"});
			req.sendResponse(std::move(hdr),res.stringify());
			return true;
		} else if (entry == "licence") {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			req.sendResponse(std::move(hdr),Value(binfo.licence).stringify());
		} else if (entry == "icon.png") {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			Value v = base64->decodeBinaryValue(binfo.favicon);
			Binary b = v.getBinary(base64);
			req.sendResponse(
					HTTPResponse(200).contentType("image/png").cacheFor(600),
					StrViewA(b));
			return true;
		} else if (entry == "apikey") {
			IApiKey *kk = dynamic_cast<IApiKey*>(api);
			if (kk == nullptr) {
				req.sendErrorPage(403);
				return true;
			}
			if (!req.allowMethods( { "GET" }))
				return true;
			req.sendResponse(std::move(hdr),
					kk->getApiKeyFields().toString().str());
			return true;
		} else if (entry == "settings") {

			IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api);
			if (bc == nullptr) {
				req.sendErrorPage(403);return true;
			}
			if (!req.allowMethods( { "GET", "PUT" })) return true;
			if (req.getMethod() == "GET") {
				req.sendResponse(std::move(hdr), Value(bc->getSettings("")).stringify());
			} else {
				Stream s = req.getBodyStream();
				Value v = Value::parse(s);
				bc->setSettings(v);
				req.sendResponse("application/json","true",202);
				return true;
			}
		} else if (entry == "pairs") {
			if (pair.empty()) {
				if (!req.allowMethods( { "GET" }))
					return true;
				Array p;
				auto pairs = api->getAllPairs();
				for (auto &&k : pairs)
					p.push_back(k);
				Object obj("entries", p);
				req.sendResponse(std::move(hdr), Value(obj).stringify());
				return true;
			} else {
				std::string p = urlDecode(pair);

				try {
					if (orders.empty()) {
						if (!req.allowMethods( { "GET" }))
							return true;
						Value resp = getPairInfo(*api, p).replace("entries",{"orders", "ticker" });
						req.sendResponse(std::move(hdr), resp.stringify());
						return true;
					} else if (orders == "ticker") {
						if (!req.allowMethods( { "GET" }))
							return true;
						auto t = api->getTicker(p);
						Value ticker = Object("ask", t.ask)("bid", t.bid)(
								"last", t.last)("time", t.time);
						req.sendResponse(std::move(hdr), ticker.stringify());
						return true;
					}  else if (orders == "settings") {

						IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api);
						if (bc == nullptr) {
							req.sendErrorPage(403);return true;
						}
						if (!req.allowMethods( { "GET", "PUT" })) return true;
						if (req.getMethod() == "GET") {
							req.sendResponse(std::move(hdr), Value(bc->getSettings(pair)).stringify());
						} else {
							Stream s = req.getBodyStream();
							Value v = Value::parse(s);
							bc->setSettings(v);
							req.sendResponse("application/json","true",202);
							return true;
						}
					}else if (orders == "orders") {
						if (!req.allowMethods( { "GET", "POST", "DELETE" }))
							return true;
						if (req.getMethod() == "GET") {
							Value orders = getOpenOrders(*api, p);
							req.sendResponse(std::move(hdr),
									orders.stringify());
							return true;
						} else if (req.getMethod() == "DELETE") {
							api->reset();
							auto ords = api->getOpenOrders(p);
							for (auto &&x : ords) {
								api->placeOrder(p, 0, 0, Value(), x.id, 0);
							}
							api->reset();
							req.sendResponse(std::move(hdr), "true");
							return true;
						} else {
							api->reset();
							Stream s = req.getBodyStream();
							Value parsed = Value::parse(s);
							Value price = parsed["price"];
							if (price.type() == json::string) {
								auto ticker = api->getTicker(pair);
								if (price.getString() == "ask")
									price = ticker.ask;
								else if (price.getString() == "bid")
									price = ticker.bid;
								else {
									req.sendErrorPage(400, "", "Invalid price");
									return true;
								}
							}
							Value res = api->placeOrder(pair,
									parsed["size"].getNumber(),
									price.getNumber(), parsed["clientId"],
									parsed["replaceId"],
									parsed["replaceSize"].getNumber());
							req.sendResponse(std::move(hdr), res.stringify());
							return true;
						}

					}

				} catch (...) {
					auto pp = api->getAllPairs();
					auto f = std::find(pp.begin(), pp.end(), p);
					if (f == pp.end()) {
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
	return true;

}

bool WebCfg::reqTraders(simpleServer::HTTPRequest req, ondra_shared::StrViewA vpath) const {
	std::string path = vpath;
	dispatch([path,req, this]() mutable {
		HTTPResponse hdr(200);
		hdr.contentType("application/json");

		try {
			if (path.empty()) {
				if (!req.allowMethods({"GET"})) return ;
				Value res (json::array, trlist.begin(), trlist.end(), [&](auto &&x) {
					return x.first;
				});
				res = Object("entries", res);
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
							Value(Object("entries",{"stop","reset","repair","broker","trading","spread"})).stringify());
					}
				} else {
					tr->init();
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
					} else if (cmd == "broker") {
						StrViewA nx = splt();
						StrViewA vpath = path;
						StrViewA restpath = vpath.substr(nx.data - vpath.data);
						reqBrokerSpec(req, restpath, &(tr->getBroker()));
					} else if (cmd == "trading") {
						Object out;
						auto chart = tr->getChart();
						auto &&broker = tr->getBroker();
						broker.reset();
						if (chart.length>600) chart = chart.substr(chart.length-600);
						out.set("chart", Value(json::array,chart.begin(), chart.end(),[&](auto &&item) {
							return Object("time", item.time)("last",item.last);
						}));
						std::size_t start = chart.empty()?0:chart[0].time;
						auto trades = tr->getTrades();
						out.set("trades", Value(json::array, trades.begin(), trades.end(),[&](auto &&item) {
							if (item.time >= start) return item.toJSON(); else return Value();
						}));
						out.set("ticker", ([&](auto &&t) {
							return Object("ask", t.ask)("bid", t.bid)("last", t.last)("time", t.time);
						})(broker.getTicker(tr->getConfig().pairsymb)));
						out.set("orders", getOpenOrders(broker, tr->getConfig().pairsymb));
						out.set("broker", tr->getConfig().broker);
						auto ibalance = tr->getInternalBalance();
						out.set("pair", getPairInfo(broker, tr->getConfig().pairsymb, ibalance));
						req.sendResponse(std::move(hdr), Value(out).stringify());
					} else if (cmd == "spread") {
						if (!req.allowMethods({"POST"})) return;
						Value args = Value::parse(req.getBodyStream());
						Value sma = args["sma"];
						Value stdev = args["stdev"];
						Value mult = args["mult"];
						auto res = tr->visualizeSpread(sma.getUInt(), stdev.getUInt(),mult.getNumber());
						Value out (json::object,
							{Value("chart",Value(json::array, res.chart.begin(), res.chart.end(), [](auto &&k){
								return Value(json::object,{
									Value("p",k.price),
									Value("l",k.low),
									Value("h",k.high),
									Value("s",k.size),
									Value("t",k.time),
								});
							}))}
						);
						req.sendResponse(std::move(hdr), out.stringify());
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
		try {
			MTrader_Config cfg;
			cfg.loadConfig(v, t.test);
			t.addTrader(cfg,v.getKey());
			traderNames.push_back(v.getKey());
		} catch (std::exception &e) {
			logError("Failed to initialized trader $1 - $2", v.getKey(), e.what());
		}
	}
	Value newInterval = data["report_interval"];
	if (newInterval.defined()) {
		t.rpt.setInterval(newInterval.getUInt());
	}
}

void WebCfg::State::setAdminAuth(StrViewA auth) {
	auto ulist = AuthUserList::decodeMultipleBasicAuth(auth);
	auto ulist2 = ulist;
	users->setCfgUsers(std::move(ulist));
	admins->setCfgUsers(std::move(ulist2));
}

void WebCfg::State::setAdminUser(const std::string &uname, const std::string &pwd) {
	auto hash = AuthUserList::hashPwd(uname,pwd);
	users->setUser(uname, hash);
	admins->setUser(uname, hash);
}


void WebCfg::State::init() {
	init(config->load());
}

bool WebCfg::reqLogout(simpleServer::HTTPRequest req, bool commit) const {

	auto hdr = req["Authorization"];
	auto hdr_splt = hdr.split(" ");
	hdr_splt();
	StrViewA cred = hdr_splt();
	auto credobj = AuthUserList::decodeBasicAuth(cred);
	if (commit) {
		if (state->logout_commit(std::move(credobj.first)))
			auth.genError(req);
		else
			req.sendResponse("text/plain","");
	} else {
		state->logout_user(std::move(credobj.first));
		std::string rndstr;
		std::time_t t = std::time(nullptr);
		rndstr = "?";
		ondra_shared::unsignedToString(t,[&](char c){rndstr.push_back(c);},16,8);
		req.redirect(strCommand[logout_commit].data+rndstr,Redirect::temporary_GET);
	}

	return true;
}

bool WebCfg::reqStop(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST"})) return true;

	dispatch([=]{

		HTTPResponse hdr(200);
		hdr.contentType("application/json");

		for (auto &&x: trlist) {
			x.second->stop();
		};
		trlist.resetBrokers();
		trlist.runTraders(true);
		trlist.rpt.genReport();
		trlist.resetBrokers();
		req.sendResponse(std::move(hdr), "true");

	});

	return true;
}

void WebCfg::State::logout_user(std::string &&user) {
	Sync _(lock);
	logout_users.insert(std::move(user));
}

bool WebCfg::State::logout_commit(std::string &&user) {
	Sync _(lock);
	if (logout_users.find(user) == logout_users.end()) {
		return false;
	} else {
		logout_users.erase(user);
		return true;
	}
}

bool WebCfg::reqEditor(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST"})) return true;
	req.readBodyAsync(10000,[&trlist = this->trlist, state = this->state](simpleServer::HTTPRequest req) {

		Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
		Value broker = data["broker"];
		Value trader = data["trader"];
		Value pair = data["pair"];
		std::string p;

		Sync _(state->lock);
		NamedMTrader *tr = trlist.find(trader.toString().str());
		IStockApi *api = nullptr;
		if (tr == nullptr) {
			api = trlist.stockSelector.getStock(broker.toString().str());
		} else {
			api = &tr->getBroker();
		}
		if (api == nullptr) {
			return req.sendErrorPage(404);
		}
		if (tr && !pair.hasValue()) {
			p = tr->getConfig().pairsymb;
		} else {
			p = pair.toString().str();
		}


		api->reset();
		auto binfo = api->getBrokerInfo();
		auto pairinfo = api->getMarketInfo(p);


		Value strategy;
		if (tr) {
			strategy = tr->getStrategy().exportState();
		}

		Object result;
		result.set("broker",Object
				("name", binfo.name)
				("exchangeName", binfo.exchangeName)
				("version", binfo.version)
				("settings", binfo.settings)
				("trading_enabled", binfo.trading_enabled));
		result.set("pair", getPairInfo(*api, p, tr->getInternalBalance()));
		result.set("orders", getOpenOrders(*api, p));
		result.set("strategy", strategy);

		req.sendResponse("application/json", Value(result).stringify());
	});
	return true;
}

