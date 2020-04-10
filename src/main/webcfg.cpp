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
	{WebCfg::login, "login"},
	{WebCfg::logout_commit, "logout_commit"},
	{WebCfg::editor, "editor"},
	{WebCfg::backtest, "backtest"},
	{WebCfg::spread, "spread"},
	{WebCfg::upload_prices, "upload_prices"},
	{WebCfg::upload_trades, "upload_trades"}
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
		case login: return reqLogin(req);
		case logout_commit: return reqLogout(req,true);
		case editor: return reqEditor(req);
		case backtest: return reqBacktest(req);
		case spread: return reqSpread(req);
		case upload_prices: return reqUploadPrices(req);
		case upload_trades: return reqUploadTrades(req);
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


		req.readBodyAsync(1024*1024, [state,&traders,dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
			try {
				Sync _(state->lock);

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
				data = data.replace("brokers", state->broker_config);
				Value apikeys = data["apikeys"];
				state->config->store(data.replace("apikeys", Value()));
				state->write_serial = serial+1;;

				dispatch([&traders, state, req, data, apikeys] {
					try {
						Sync _(state->lock);
						state->applyConfig(traders);
						if (apikeys.type() == json::object) {
							for (Value v: apikeys) {
								StrViewA broker = v.getKey();
								traders.stockSelector.checkBrokerSubaccount(broker);
								IStockApi *b = traders.stockSelector.getStock(broker);
								if (b) {
									IApiKey *apik = dynamic_cast<IApiKey *>(b);
									apik->setApiKey(v);
								}
							}
						}
					} catch (std::exception &e) {
						req.sendErrorPage(500,StrViewA(),e.what());
						return;
					}
					try {
						req.sendResponse(HTTPResponse(202).contentType("application/json"),data.stringify());
						traders.runTraders(true);
						traders.rpt.genReport();
					} catch (std::exception &e) {
						logError("%1", e.what());
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
	json::String url;
	if (StrViewA(binfo.exchangeUrl).begins("/")) url = {"./api/brokers/",simpleServer::urlEncode(binfo.name),"/page/"};
	else url = binfo.exchangeUrl;

	Value res = Object("name", binfo.name)("trading_enabled",
			binfo.trading_enabled)("exchangeName", binfo.exchangeName)(
			"exchangeUrl", url)("version", binfo.version)("subaccounts",binfo.subaccounts);
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
			trlist.stockSelector.forEachStock([&](std::string_view n, IStockApi &api) {
				res.push_back(brokerToJSON(api.getBrokerInfo()));
			});
			req.sendResponse("application/json",Value(res).stringify());
			return true;
		}
		std::string broker = urlDecode(urlbroker);
		IStockApi *api = trlist.stockSelector.getStock(broker);

		return reqBrokerSpec(req, splt, api,broker);
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

static Value getPairInfo(IStockApi &api, const std::string_view &pair, const std::optional<double> &internalBalance = std::optional<double>()
		, const std::optional<double> &internalCurrencyBalance = std::optional<double>()) {
	IStockApi::MarketInfo nfo = api.getMarketInfo(pair);
	double ab = internalBalance.has_value()?*internalBalance:getSafeBalance(&api, nfo.asset_symbol, pair);
	double cb = internalCurrencyBalance.has_value()?*internalCurrencyBalance:getSafeBalance(&api, nfo.currency_symbol, pair);
	Value last;
	try {
		auto ticker = api.getTicker(pair);
		last = ticker.last;
	} catch (std::exception &e) {
		last = e.what();
	}
	Value resp = Object("symbol",pair)("asset_symbol", nfo.asset_symbol)(
			"currency_symbol", nfo.currency_symbol)("fees",
			nfo.fees)("leverage", nfo.leverage)(
			"invert_price", nfo.invert_price)(
			"asset_balance", ab)("currency_balance", cb)(
			"min_size", nfo.min_size)("price",last);
	return resp;

}

bool WebCfg::reqBrokerSpec(simpleServer::HTTPRequest req,
		ondra_shared::StrViewA vpath, IStockApi *api, ondra_shared::StrViewA broker_name) const {

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
			Value res = brokerToJSON(binfo).replace("entries", { "icon.png", "pairs","apikey","licence","page","subaccount" });
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
		} else if (entry == "subaccount") {
			if (!req.allowMethods( { "POST" }))
				return true;
			auto binfo = api->getBrokerInfo();
			if (!binfo.subaccounts) {
				req.sendErrorPage(403);
			} else {
				json::Value n = json::Value::parse(req.getBodyStream());
				if (n.toString().length()>20) {
					req.sendErrorPage(415);
				} else {
					dispatch([=]{
						std::string newname = binfo.name + "~";
						for (auto &&k: n.getString()) if (isalnum(k)) newname.push_back(k);
						trlist.stockSelector.checkBrokerSubaccount(newname);
						req.sendResponse("application/json", Value(newname).stringify());
					});
				}
			}
			return true;
		}else if (entry == "page") {
			IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api);
			if (bc == nullptr) {
				req.sendErrorPage(403);return true;
			}
			StrViewA path = req.getPath();
			auto pos = path.indexOf(entry)+4;
			std::string vpath = path.substr(pos);
			if (vpath.empty()) return req.redirectToFolderRoot();
			std::string method = req.getMethod();
			IBrokerControl::PageData d;
			for (auto &&h: req) {
				if (h.first != "Authorization") d.headers.emplace_back(h.first, h.second);
			}
			req.readBodyAsync(1024*1024, [=](simpleServer::HTTPRequest req) mutable {
				d.body.append(reinterpret_cast<const char *>(req.getUserBuffer().data()), req.getUserBuffer().size());
				IBrokerControl::PageData r = bc->fetchPage(method,vpath,d);
				if (r.code == 0) {
					req.sendErrorPage(404);
					return true;
				}
				simpleServer::HTTPResponse hdr(r.code);
				for (auto &&k : r.headers) hdr(k.first,k.second);
				req.sendResponse(hdr,r.body);
				return true;

			});
			return true;
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
						Value resp = getPairInfo(*api, p).replace("entries",{"orders", "ticker", "settings"});
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
							Value res = bc->setSettings(v);
							if (!res.defined()) res = true;
							else state->setBrokerConfig(broker_name, res);
							req.sendResponse("application/json", res.stringify(), 202);
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
							Value(Object("entries",{"stop","reset","repair","broker","trading","strategy"})).stringify());
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
						std::string brokerName = tr->getConfig().broker;
						reqBrokerSpec(req, restpath, &(tr->getBroker()), brokerName);
					} else if (cmd == "trading") {
						Object out;
						auto chartx = tr->getChart();
						StringView<MTrader::ChartItem> chart(chartx.data(), chartx.size());
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
						auto ibalance = MTrader::getInternalBalance(tr);
						out.set("pair", getPairInfo(broker, tr->getConfig().pairsymb, ibalance));
						req.sendResponse(std::move(hdr), Value(out).stringify());
					} else if (cmd == "strategy") {
						if (!req.allowMethods({"GET","PUT"})) return;
						Strategy strategy = tr->getStrategy();
						if (req.getMethod() == "GET") {
							auto st = tr->getMarketStatus();
							json::Value v = strategy.exportState();
							if (tr->getConfig().internal_balance) {
								v = v.replace("internal_balance", Object
										("assets", st.assetBalance)
										("currency", st.currencyBalance)
									);
							}
							req.sendResponse(std::move(hdr), v.stringify());
						} else {
							json::Value v = json::Value::parse(req.getBodyStream());
							strategy.importState(v);
							auto st = v["internal_balance"];
							if (st.hasValue()) {
								Value assets = st["assets"];
								Value currency = st["currency"];
								tr->setInternalBalancies(assets.getNumber(), currency.getNumber());
							}
							if (!strategy.isValid()) {
								req.sendErrorPage(409,"","Settings was not accepted");
							} else {
								tr->setStrategy(strategy);
								req.sendResponse("application/json","true",202);
							}
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
			UserVector &&curVal, Value r){
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
	t.stockSelector.eraseSubaccounts();

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

	Value bc = data["brokers"];
	broker_config = bc;
	t.stockSelector.forEachStock([&](std::string_view name, IStockApi &api) {
		Value b = bc[name];
		if (b.defined()) {
			IBrokerControl *eapi = dynamic_cast<IBrokerControl *>(&api);
			if (eapi) {
				eapi->restoreSettings(b);
			}
		}
	});

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
	req.readBodyAsync(10000,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req) {
		dispatch([req,&trlist, state ]()mutable{
			try {

				Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value broker = data["broker"];
				Value trader = data["trader"];
				Value pair = data["pair"];
				std::string p;

				Sync _(state->lock);
				trlist.stockSelector.checkBrokerSubaccount(broker.getString());
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
				Value position;
				if (tr) {
					strategy = tr->getStrategy().dumpStatePretty(tr->getMarketInfo());
					auto trades = tr->getTrades();
					auto pos = std::accumulate(trades.begin(), trades.end(),0.0,[&](
							auto &&a, auto &&b
					) {
						return a + b.eff_size;
					});
					position = pos;
				}

				Object result;
				result.set("broker",Object
						("name", binfo.name)
						("exchangeName", binfo.exchangeName)
						("version", binfo.version)
						("settings", binfo.settings)
						("trading_enabled", binfo.trading_enabled));
				result.set("pair", getPairInfo(*api, p, MTrader::getInternalBalance(tr), MTrader::getInternalCurrencyBalance(tr)));
				result.set("orders", getOpenOrders(*api, p));
				result.set("strategy", strategy);
				result.set("position", position);

				req.sendResponse("application/json", Value(result).stringify());
			} catch (std::exception &e) {
				req.sendErrorPage(500, StrViewA(), e.what());
			}
		});
	});
	return true;
}

bool WebCfg::reqLogin(simpleServer::HTTPRequest req) const {
	req.redirect("../index.html", simpleServer::Redirect::temporary_repeat);
	return true;
}

void WebCfg::State::setBrokerConfig(json::StrViewA name, json::Value config) {
	Sync _(lock);
	broker_config = broker_config.getValueOrDefault(Value(json::object)).replace(name,config);
}

bool WebCfg::reqBacktest(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST","DELETE"})) return true;
	if (req.getMethod() == "DELETE") {
		Sync _(state->lock);
		state->backtest_cache.clear();
		state->prices_cache.clear();
		state->spread_cache.clear();
		_.unlock();
		req.sendResponse("application/json","true");
		return true;
	} else  {
		req.readBodyAsync(50000,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req)mutable{
			try {
				Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value id = data["id"];


				auto process=[=](Traders &trlist, const BacktestCache::Subj &trades) {

					Value config = data["config"];
					Value init_pos = data["init_pos"];
					Value balance = data["balance"];
					Value fill_atprice= data["fill_atprice"];
					std::uint64_t start_date=data["start_date"].getUIntLong();

					MTrader_Config mconfig;
					mconfig.loadConfig(config,false);
					auto piter = trades.prices.begin();
					auto pend = trades.prices.end();

					BTTrades rs = backtest_cycle(mconfig, [&]{
						std::optional<BTPrice> x;
						while (piter != pend && piter->time < start_date) ++piter;
						if (piter != pend) {
							x = *piter;
							++piter;
						}
						return x;
					}, trades.minfo,init_pos.getNumber(), balance.getNumber(), fill_atprice.getBool());

					Value result (json::array, rs.begin(), rs.end(), [](const BTTrade &x) {
						return Object
								("np",x.neutral_price)
								("op",x.open_price)
								("na",x.norm_accum)
								("npl",x.norm_profit)
								("npla",x.norm_profit_total)
								("pl",x.pl)
								("ps",x.pos)
								("pr",x.price.price)
								("tm",x.price.time)
								("sz",x.size);
					});
					String resstr = result.toString();
					req.sendResponse("application/json",resstr.str());
				};




				Sync _(state->lock);
				if (state->backtest_cache.available(id.toString().str())) {
					auto t = state->backtest_cache.getSubject();
					_.unlock();
					process(trlist, t);
				} else {

					dispatch([&trlist, state, req, id, process = std::move(process)]()mutable {
						try {
							MTrader *tr = trlist.find(id.getString());
							if (tr == nullptr) {
								req.sendErrorPage(404);
								return;
							}

							const auto &tradeHist = tr->getTrades();
							BacktestCacheSubj trs;
							std::transform(tradeHist.begin(),tradeHist.end(),
									std::back_insert_iterator(trs.prices),[](const IStatSvc::TradeRecord &r) {
								return BTPrice{r.time, r.price};
							});
							trs.minfo = tr->getMarketInfo();

							Sync _(state->lock);
							state->backtest_cache = BacktestCache(trs, id.toString().str());
							_.unlock();
							process(trlist, trs);
						} catch (std::exception &e) {
							req.sendErrorPage(400,"", e.what());
						}
					});
				}
			} catch (std::exception &e) {
				req.sendErrorPage(400,"", e.what());
			}

		});
		return true;
	}
}

template<typename Iter>
class IterFn{
public:
	IterFn(Iter beg, Iter end):beg(beg), end(end) {}
	Iter beg,end;
	auto operator()() {
		using T = std::remove_reference_t<decltype(*beg)>;
		if (beg == end) return std::optional<T>();
		else return std::optional<T>(*(beg++));
	}
};

bool WebCfg::reqSpread(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST"})) return true;
		req.readBodyAsync(50000,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req)mutable{
		try {
			Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
			Value id = args["id"];
			auto process = [=](const SpreadCacheItem &data) {

				Value sma = args["sma"];
				Value stdev = args["stdev"];
				Value mult = args["mult"];
				Value dynmult_raise = args["raise"];
				Value dynmult_fall = args["fall"];
				Value dynmult_mode = args["mode"];
				Value dynmult_sliding = args["sliding"];
				Value dynmult_mult = args["dyn_mult"];

				auto res = MTrader::visualizeSpread(IterFn(data.chart.begin(),data.chart.end()),sma.getUInt(), stdev.getUInt(),mult.getNumber(),
						dynmult_raise.getValueOrDefault(1.0),
						dynmult_fall.getValueOrDefault(1.0),
						dynmult_mode.getValueOrDefault("independent"),
						dynmult_sliding.getBool(),
						dynmult_mult.getBool(),
						true,false);
				if (data.invert_price) {
					std::transform(res.chart.begin(), res.chart.end(), res.chart.begin(),[](const MTrader::VisRes::Item &itm) {
						return MTrader::VisRes::Item{1.0/itm.price,1.0/itm.high, 1.0/itm.low,-itm.size,itm.time};
					});
				}

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
				req.sendResponse("application/json", out.stringify());
			};

			Sync _(state->lock);
			if (state->spread_cache.available(id.toString().str())) {
				auto t = state->spread_cache.getSubject();
				_.unlock();
				process(t);
			} else {
				dispatch([&trlist, state, req, id, process = std::move(process)]()mutable {
					try {
						MTrader *tr = trlist.find(id.getString());
						if (tr == nullptr) {
							req.sendErrorPage(404);
							return;
						}
						SpreadCacheItem x;
						x.chart = tr->getChart();
						x.invert_price = tr->getMarketInfo().invert_price;
						Sync _(state->lock);
						state->spread_cache= SpreadCache(x, id.toString().str());
						_.unlock();
						process(x);
					} catch (std::exception &e) {
						req.sendErrorPage(400,"", e.what());
					}

				});
			}


		} catch (std::exception &e) {
			req.sendErrorPage(400,"",e.what());
		}
		});
	return true;
}

bool WebCfg::reqUploadPrices(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST","GET","DELETE"})) return true;
	if (req.getMethod() == "GET") {
		Sync _(state->lock);
		req.sendResponse("application/json",Value(state->upload_progress).stringify());
		return true;
	} else  if (req.getMethod() == "DELETE") {
			Sync _(state->lock);
			state->cancel_upload = true;
			req.sendResponse("application/json",Value(state->upload_progress).stringify());
			return true;
	} else {
	req.readBodyAsync(10*1024*1024,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req)mutable{
		dispatch([&trlist, state, req]()mutable{
			try {
				Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value id = args["id"];
				Value prices = args["prices"];

				if (prices.getString() == "internal") {
					Sync _(state->lock);
					state->prices_cache.clear();
					state->upload_progress = 0;
				} else if (prices.getString() == "update") {
					Sync _(state->lock);
					state->upload_progress = 0;
				} else {

					MTrader *tr = trlist.find(id.getString());
					if (tr == nullptr) {
						req.sendErrorPage(404);
						return;
					}
					IStockApi::MarketInfo minfo = tr->getMarketInfo();
					std::vector<double> chart;
					std::transform(prices.begin(), prices.end(), std::back_inserter(chart),[&](Value itm){
						double p = itm.getNumber();
						if (minfo.invert_price) p = 1.0/p;
						return p;
					});
					Sync _(state->lock);
					state->prices_cache = PricesCache(chart, id.toString().str());
					state->upload_progress = 0;
				}
				req.sendResponse("application/json", "0");
				req = nullptr;
				generateTrades(trlist, state, args);
			} catch (std::exception &e) {
				req.sendErrorPage(400,"",e.what());
			}
		});
	});
	}
	return true;
}
bool WebCfg::reqUploadTrades(simpleServer::HTTPRequest req) const {
	if (!req.allowMethods({"POST"})) return true;
	req.readBodyAsync(10*1024*1024,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req)mutable{
		dispatch([&trlist, state, req]()mutable{
			try {
				Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value id = args["id"];
				Value prices = args["prices"];
				MTrader *tr = trlist.find(id.getString());
				if (tr == nullptr) {
					req.sendErrorPage(404);
					return;
				}
				IStockApi::MarketInfo minfo = tr->getMarketInfo();
				BacktestCacheSubj bt;
				std::transform(prices.begin(), prices.end(), std::back_inserter(bt.prices), [&](const Value &itm) {
						std::uint64_t tm = itm[0].getUIntLong();
						double p = itm[1].getNumber();
						if (minfo.invert_price) p = 1.0/p;
						return BTPrice{tm, p};
				});
				bt.minfo = minfo;
				Sync _(state->lock);
				state->upload_progress = -1;
				state->backtest_cache = BacktestCache(bt, id.toString().str());
				req.sendResponse("application/json", "true");
			} catch (std::exception &e) {
				req.sendErrorPage(400,"",e.what());
			}
		});
	});
	return true;
}
bool WebCfg::generateTrades(Traders &trlist, ondra_shared::RefCntPtr<State> state, json::Value args) {
	try {
		Value id = args["id"];
		Value sma = args["sma"];
		Value stdev = args["stdev"];
		Value mult = args["mult"];
		Value dynmult_raise = args["raise"];
		Value dynmult_fall = args["fall"];
		Value dynmult_mode = args["mode"];
		Value dynmult_sliding = args["sliding"];
		Value dynmult_mult = args["dyn_mult"];


		MTrader *tr = trlist.find(id.getString());
		if (tr == nullptr) {
			state->upload_progress = -1;
			return false;
		}

		std::function<std::optional<MTrader::ChartItem>()> source;
		Sync _(state->lock);
		state->upload_progress = 0;
		if (!state->prices_cache.available(id.getString())) {
			auto chart = tr->getChart();
			source = [=,pos = std::size_t(0) ]() mutable {
				if (state->cancel_upload || pos >= chart.size()) {
					return std::optional<MTrader::ChartItem>();
				}
				state->upload_progress = (pos * 100)/chart.size();
				return std::optional<MTrader::ChartItem>(chart[pos++]);
			};
		} else {
			auto prc = state->prices_cache.getSubject();
			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			source = [prc = std::move(prc), pos = std::size_t(0), state , now]() mutable {
				std::size_t sz = prc.size();
				if (state->cancel_upload || pos >= sz) {
					return std::optional<MTrader::ChartItem>();
				}
				double p = prc[pos++];
				state->upload_progress = (pos * 100)/sz;
				return std::optional<MTrader::ChartItem>(MTrader::ChartItem{now - (sz - pos)*60000,p,p,p});
			};
		}
		state->cancel_upload = false;
		_.unlock();
		MTrader::VisRes trades = MTrader::visualizeSpread(std::move(source),sma.getNumber(),stdev.getNumber(),mult.getNumber(),
				dynmult_raise.getValueOrDefault(1.0),
				dynmult_fall.getValueOrDefault(1.0),
				dynmult_mode.getValueOrDefault("independent"),
				dynmult_sliding.getBool(),
				dynmult_mult.getBool(),
				false,true);

		BacktestCacheSubj bt;
		std::transform(trades.chart.begin(), trades.chart.end(), std::back_inserter(bt.prices), [](const MTrader::VisRes::Item &itm) {
				return BTPrice{itm.time, itm.price};
		});
		bt.minfo = tr->getMarketInfo();
		_.lock();
		state->upload_progress = -1;
		state->backtest_cache = BacktestCache(bt, id.toString().str());
		_.unlock();
		return true;
	} catch (std::exception &e) {
		logError("Error: $1", e.what());
		Sync _(state->lock);
		state->upload_progress = -1;
		_.unlock();
		return false;
	}

}

