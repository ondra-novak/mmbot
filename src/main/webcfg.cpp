/*
 * webcfg.cpp
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */


#include "webcfg.h"

#include <random>
#include <unordered_set>

#include <imtjson/array.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include "../brokers/httpjson.h"
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
#include "random_chart.h"
#include "sgn.h"
#include "spread.h"

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
	{WebCfg::backtest2, "backtest2"},
	{WebCfg::spread, "spread"},
	{WebCfg::strategy, "strategy"},
	{WebCfg::wallet, "wallet"},
	{WebCfg::btdata, "btdata"},
	{WebCfg::visstrategy, "visstrategy"},
	{WebCfg::utilization, "utilization"},
	{WebCfg::progress, "progress"},
	{WebCfg::news, "news"}
});

WebCfg::WebCfg( const SharedObject<State> &state,
		const std::string &realm,
		const SharedObject<Traders> &traders,
		Dispatch &&dispatch,
		json::PJWTCrypto jwt,
		SharedObject<AbstractExtern> backtest_broker,
		std::size_t upload_limit
)
	:auth(realm, state.lock_shared()->admins,jwt, false)
	,trlist(traders)
	,dispatch(std::move(dispatch))
	,state(state)
	,backtest_broker(backtest_broker)
	,upload_limit(upload_limit)
{

}

WebCfg::~WebCfg() {
}

bool WebCfg::operator ()(const simpleServer::HTTPRequest &req,
		const ondra_shared::StrViewA &vpath)  {

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
		case backtest: return reqBacktest_v2(req, rest);
		case backtest2: return reqBacktest_v2(req, rest);
		case spread: return reqSpread(req);
		case strategy: return reqStrategy(req);
		case wallet: return reqDumpWallet(req, rest);
		case btdata: return reqBTData(req);
		case visstrategy: return reqVisStrategy(req, qp);
		case utilization: return reqUtilization(req, qp);
		case progress: return reqProgress(req,rest);
		case news: return reqNews(req);
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

bool WebCfg::reqConfig(simpleServer::HTTPRequest req)  {

	if (!req.allowMethods({"GET","PUT"})) return true;
	if (req.getMethod() == "GET") {

		json::Value data = state.lock_shared()->config->load();
		if (!data.defined()) data = Object({{"revision",0}});
		req.sendResponse("application/json",data.stringify().str());

	} else {


		req.readBodyAsync(upload_limit, [state=this->state,traders=this->trlist,dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
			try {
				auto lkst = state.lock();
				Value data = Value::fromString(map_bin2str(req.getUserBuffer()));
				unsigned int serial = data["revision"].getUInt();
				if (serial != lkst->write_serial) {
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
				data.setItems({
					{"revision",lkst->write_serial+1},
					{"brokers", lkst->broker_config},
					{"news_tm", lkst->news_tm}
				});
				Value apikeys = data["apikeys"];
				lkst->config->store(data.replace("apikeys", Value()));
				lkst->write_serial = serial+1;;


				try {
					lkst->applyConfig(traders);
					if (apikeys.type() == json::object) {
						for (Value v: apikeys) {
							StrViewA broker = v.getKey();
							auto trs = traders.lock();
							trs->stockSelector.checkBrokerSubaccount(broker);
							PStockApi b = trs->stockSelector.getStock(broker);
							if (b != nullptr) {
								IApiKey *apik = dynamic_cast<IApiKey *>(b.get());
								apik->setApiKey(v);
							}
						}
					}
				} catch (std::exception &e) {
					req.sendErrorPage(500,StrViewA(),e.what());
					return;
				}
				req.sendResponse(HTTPResponse(202).contentType("application/json"),data.stringify().str());


			} catch (std::exception &e) {
				req.sendErrorPage(500,StrViewA(),e.what());
			}

		});


	}
	return true;


}


bool WebCfg::reqSerial(simpleServer::HTTPRequest req) {
	if (!req.allowMethods({"GET"})) return true;
	req.sendResponse("text/plain") << serial << "\r\n";
	return true;
}


static double getSafeBalance(const PStockApi &api, std::string_view symb,  std::string_view pair) {
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

	Value res = Object({
		{"name", binfo.name},
		{"trading_enabled",binfo.trading_enabled},
		{"exchangeName", binfo.exchangeName},
		{"exchangeUrl", url},
		{"version", binfo.version},
		{"subaccounts",binfo.subaccounts}});
	return res;
}


bool WebCfg::reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest)  {
	if (rest.empty()) {
		if (!req.allowMethods({"GET"})) return true;
		Array brokers;
		trlist.lock_shared()->stockSelector.forEachStock([&](const std::string_view &name,const PStockApi &){
			brokers.push_back(name);
		});
		Object obj({{"entries", brokers}});
		req.sendResponse("application/json",Value(obj).stringify().str());
		return true;
	} else {
		std::string vpath = rest;
		auto splt = StrViewA(vpath).split("/",2);
		StrViewA urlbroker = splt();
		if (urlbroker == "_reload") {
			if (!req.allowMethods({"POST"})) return true;
			dispatch([=] {
				trlist.lock_shared()->stockSelector.forEachStock([&](const std::string_view &,const PStockApi &x){
					ExtStockApi *ex = dynamic_cast<ExtStockApi *>(x.get());
					ex->stop();
				});
				req.sendResponse("application/json","true");
			});
			return true;
		} else if (urlbroker == "_all") {
			if (!req.allowMethods({"GET"})) return true;
			Array res;
			trlist.lock_shared()->stockSelector.forEachStock([&](std::string_view n, const PStockApi &api) {
				res.push_back(brokerToJSON(api->getBrokerInfo()));
			});
			req.sendResponse("application/json",Value(res).stringify().str());
			return true;
		}
		std::string broker = urlDecode(urlbroker);
		PStockApi api = trlist.lock_shared()->stockSelector.getStock(broker);

		return reqBrokerSpec(req, splt, api,broker);
	}
}

static Value getOpenOrders(const PStockApi &api, const std::string_view &pair) {
	Value orders = json::array;
	try {
		auto ords = api->getOpenOrders(pair);
		orders = Value(json::array, ords.begin(), ords.end(),
				[&](const IStockApi::Order &ord) {
					return Object({{"price", ord.price},{
							"size", ord.size},{"clientId",
							ord.client_id},{"id", ord.id}});
				});
	} catch (...) {

	}
	return orders;

}

static Value getPairInfo(const PStockApi &api, const std::string_view &pair, const std::optional<double> &internalBalance = std::optional<double>()
		, const std::optional<double> &internalCurrencyBalance = std::optional<double>()) {
	IStockApi::MarketInfo nfo = api->getMarketInfo(pair);
	double ab = internalBalance.has_value()?*internalBalance:getSafeBalance(api, nfo.asset_symbol, pair);
	double cb = internalCurrencyBalance.has_value()?*internalCurrencyBalance:getSafeBalance(api, nfo.currency_symbol, pair);
	Value last;
	try {
		auto ticker = api->getTicker(pair);
		last = ticker.last;
	} catch (std::exception &e) {
		last = e.what();
	}

	Value quote_currency = nfo.invert_price?Value(nfo.inverted_symbol):Value(nfo.currency_symbol);
	Value quote_asset = nfo.invert_price?Value(nfo.currency_symbol):Value(nfo.asset_symbol);

	Value resp = nfo.toJSON();
	resp.setItems({
		{"symbol",pair},
		{"asset_balance", ab},
		{"currency_balance", cb},
		{"price",last},
		{"quote_currency", quote_currency},
		{"quote_asset", quote_asset},
	});


	return resp;

}

bool WebCfg::reqBrokerSpec(simpleServer::HTTPRequest req,
		ondra_shared::StrViewA vpath, PStockApi api, ondra_shared::StrViewA broker_name)  {

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

		std::string pairstr = urlDecode(pair);
		pair = pairstr;

		if (req.getPath().indexOf("reset=1") != StrViewA::npos) {
			api->reset();
		}

		if (entry.empty()) {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			Value res = brokerToJSON(binfo).replace("entries", { "icon.png", "pairs","apikey","licence","page","subaccount" });
			req.sendResponse(std::move(hdr),res.stringify().str());
			return true;
		} else if (entry == "licence") {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			req.sendResponse(std::move(hdr),Value(binfo.licence).stringify().str());
		} else if (entry == "icon.png") {
			if (!req.allowMethods( { "GET" }))
				return true;
			auto binfo = api->getBrokerInfo();
			req.sendResponse(
					HTTPResponse(200).contentType("image/png").cacheFor(600),binfo.favicon);
			return true;
		} else if (entry == "apikey") {
			IApiKey *kk = dynamic_cast<IApiKey*>(api.get());
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
					std::string newname = binfo.name + "~";
					for (auto &&k: n.getString()) if (isalnum(k)) newname.push_back(k);
					auto trl = trlist;
					trl.lock()->stockSelector.checkBrokerSubaccount(newname);
					req.sendResponse("application/json", Value(newname).stringify().str());
				}
			}
			return true;
		}else if (entry == "page") {
			IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api.get());
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
			req.readBodyAsync(upload_limit, [=](simpleServer::HTTPRequest req) mutable {
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
				try {
					auto bk = dynamic_cast<const IBrokerControl *>(api.get());
					if (bk) {
						Value v = bk->getMarkets();
						Value entries; {
							Array p;
							v.walk([&](Value z) {
								if (z.type() == json::string) {
									p.push_back(z.stripKey());
								}
								return true;
							});
							entries = p;
						}
						Value result(json::object,{
								Value("entries", entries.sort(Value::compare).uniq()),
								Value("struct", v)
						});
						req.sendResponse(std::move(hdr), Value(result).stringify().str());
						return true;
					}
				} catch (...) {

				}
				Array p;
				auto pairs = api->getAllPairs();
				for (auto &&k : pairs)
					p.push_back(k);
				Object obj({{"entries", p}});
				req.sendResponse(std::move(hdr), Value(obj).stringify().str());
				return true;
			} else {
				std::string p = urlDecode(pair);

				try {
					if (orders.empty()) {
						if (!req.allowMethods( { "GET" }))
							return true;
						Value resp = getPairInfo(api, p).replace("entries",{"orders", "ticker", "settings","info","history"});
						req.sendResponse(std::move(hdr), resp.stringify().str());
						return true;
					} else if (orders == "ticker") {
						if (!req.allowMethods( { "GET" }))
							return true;
						auto t = api->getTicker(p);
						Value ticker = Object({{"ask", t.ask},{"bid", t.bid},{
								"last", t.last},{"time", t.time}});
						req.sendResponse(std::move(hdr), ticker.stringify().str());
						return true;
					}  else if (orders == "info") {
						IStockApi::MarketInfo minfo = api->getMarketInfo(p);
						Value resp = minfo.toJSON();
						req.sendResponse(std::move(hdr), resp.stringify().str());
						return true;
					}  else if (orders == "settings") {

						IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api.get());
						if (bc == nullptr) {
							req.sendErrorPage(403);return true;
						}
						if (!req.allowMethods( { "GET", "PUT" })) return true;
						if (req.getMethod() == "GET") {
							req.sendResponse(std::move(hdr), Value(bc->getSettings(pair)).stringify().str());
						} else {
							Stream s = req.getBodyStream();
							Value v = Value::parse(s);
							Value res = bc->setSettings(v);
							if (!res.defined()) res = true;
							else state.lock()->setBrokerConfig(broker_name, res);
							req.sendResponse("application/json", res.stringify().str(), 202);
							return true;
						}
					}else if (orders == "orders") {
						if (!req.allowMethods( { "GET", "POST", "DELETE" }))
							return true;
						if (req.getMethod() == "GET") {
							Value orders = getOpenOrders(api, p);
							req.sendResponse(std::move(hdr),
									orders.stringify().str());
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
							req.sendResponse(std::move(hdr), res.stringify().str());
							return true;
						}

					}else if (orders == "history") {
						processBrokerHistory(req, state, api, pair);
						return true;
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

bool WebCfg::reqTraders(simpleServer::HTTPRequest req, ondra_shared::StrViewA vpath)  {
	std::string path = vpath;
	HTTPResponse hdr(200);
	hdr.contentType("application/json");

	try {
		if (path.empty()) {
			if (!req.allowMethods({"GET"})) return true;
			auto trl = trlist.lock_shared();
			Value res (json::array, trl->begin(), trl->end(), [&](auto &&x) {
				return x.first;
			});
			res = Object({{"entries", res}});
			req.sendResponse(std::move(hdr), res.stringify().str());
		} else {
			auto splt = StrViewA(path).split("/");
			std::string trid = urlDecode(StrViewA(splt()));
			auto tr = trlist.lock_shared()->find(trid);
			if (tr == nullptr) {
				req.sendErrorPage(404);
			} else if (!splt) {
				if (!req.allowMethods({"GET","DELETE"})) return true;
				if (req.getMethod() == "DELETE") {
					trlist.lock()->removeTrader(trid, false);
					req.sendResponse(std::move(hdr), "true");
				} else {
					req.sendResponse(std::move(hdr),
						Value(Object({{"entries",{"stop","clear_stats","reset","broker","trading","strategy"}}})).stringify().str());
				}
			} else {
				auto trl = tr.lock();
				trl->init();
				auto cmd = urlDecode(StrViewA(splt()));
				if (cmd == "clear_stats") {
					if (!req.allowMethods({"POST"})) return true;
					Stream s = req.getBodyStream();
					Value v = Value::parse(s);
					auto cmd = v.getString();
					if (cmd=="wipe") {
						trl->clearStats();
					} else if (cmd == "norm_recalc") {
						trl->recalcNorm();
					} else if (cmd == "norm_drops") {
						trl->fixNorm();
					} else {
						req.sendErrorPage(400);
						return true;
					}
					req.sendResponse(std::move(hdr), "true");
				} else if (cmd == "stop") {
					if (!req.allowMethods({"POST"})) return true;
					trl->stop();
					req.sendResponse(std::move(hdr), "true");
				} else if (cmd == "reset") {
					if (!req.allowMethods({"POST"})) return true;
					trl.release();
					req->readBodyAsync(1000,[tr, hdr=std::move(hdr)](HTTPRequest req) mutable {
						ondra_shared::BinaryView data = req.getUserBuffer();
						auto trl = tr.lock();
						if (data.empty()) {
							req.sendErrorPage(400);
						} else {
							auto r = json::Value::fromString(StrViewA(data));
							if  (r["alert"].getBool()) {
								trl->addAcceptLossAlert();
							}
							auto achieve = r["achieve"];
							auto cur_pct=  r["cur_pct"];
							MTrader::ResetOptions opts;
							opts.achieve = achieve.hasValue();
							opts.assets = achieve.getNumber();
							opts.cur_pct = (!cur_pct.defined()?100.0:cur_pct.getNumber())*0.01;
							trl->reset(opts);
						}
						req.sendResponse(std::move(hdr), "true");
					});
				} else if (cmd == "broker") {
					StrViewA nx = splt();
					StrViewA vpath = path;
					StrViewA restpath = vpath.substr(nx.data - vpath.data);
					std::string brokerName = trl->getConfig().broker;
					reqBrokerSpec(req, restpath, (trl->getBroker()), brokerName);
				} else if (cmd == "trading") {
					Object out;
					auto chartx = trl->getChart();
					ondra_shared::StringView<MTrader::ChartItem> chart(chartx.data(), chartx.size());
					PStockApi broker = trl->getBroker();
					broker->reset();
					if (chart.length>600) chart = chart.substr(chart.length-600);
					out.set("chart", Value(json::array,chart.begin(), chart.end(),[&](auto &&item) {
						return Object({{"time", item.time},{"last",item.last}});
					}));
					std::size_t start = chart.empty()?0:chart[0].time;
					auto trades = trl->getTrades();
					out.set("trades", Value(json::array, trades.begin(), trades.end(),[&](auto &&item) {
						if (item.time >= start) return item.toJSON(); else return Value();
					}));
					auto ticker = broker->getTicker(trl->getConfig().pairsymb);
					double stprice = strtod(splt().data,0);
					out.set("ticker", Object({{"ask", ticker.ask},{"bid", ticker.bid},{"last", ticker.last},{"time", ticker.time}}));
					out.set("orders", getOpenOrders(broker, trl->getConfig().pairsymb));
					out.set("broker", trl->getConfig().broker);
					std::optional<double> ibalance ;
					if (trl != nullptr) {
						ibalance = trl->getInternalBalance();
					}
					out.set("pair", getPairInfo(broker, trl->getConfig().pairsymb, ibalance));
					if (trl != nullptr) {
						auto strategy = trl->getStrategy();
						double assets = *trl->getPosition();
						double currencies = *trl->getCurrency();
						auto eq = strategy.getEquilibrium(assets);
						auto minfo = trl->getMarketInfo();
						if (stprice) {
							if (minfo.invert_price) stprice = 1.0/stprice;
						}else {
							stprice = ticker.last;
						}
						auto order = strategy.getNewOrder(minfo,ticker.last, stprice, sgn(eq - stprice),assets, currencies, false);
						order.price = stprice;
						minfo.addFees(order.size, order.price);
						out.set("strategy",Object({{"size", (minfo.invert_price?-1:1)*order.size}}));
					}
					req.sendResponse(std::move(hdr), Value(out).stringify().str());
				} else if (cmd == "strategy") {
					if (!req.allowMethods({"GET","PUT"})) return true;
					Strategy strategy = trl->getStrategy();
					if (req.getMethod() == "GET") {
						auto st = trl->getMarketStatus();
						json::Value v = strategy.exportState();
						if (trl->getConfig().internal_balance) {
							v = v.replace("internal_balance", Object({
									{"assets", st.assetBalance},
									{"currency", st.currencyBalance}
							}));
						}
						req.sendResponse(std::move(hdr), v.stringify().str());
					} else {
						json::Value v = json::Value::parse(req.getBodyStream());
						strategy.importState(v,trl->getMarketInfo());
						auto st = v["internal_balance"];
						if (st.hasValue()) {
							Value assets = st["assets"];
							Value currency = st["currency"];
							trl->setInternalBalancies(assets.getNumber(), currency.getNumber());
						}
						bool dry_run = v["dry_run"].getBool();
						auto pos = trl->getPosition();
						auto cur = trl->getCurrency();
						MTrader::Status mst = trl->getMarketStatus();
						strategy.onIdle(trl->getMarketInfo(), mst.ticker, *pos, *cur);
						if (!strategy.isValid()) {
							req.sendErrorPage(409,"","Settings was not accepted");
						} else {
							if (dry_run) {
								Value state = strategy.dumpStatePretty(trl->getMarketInfo());
								req.sendResponse("application/json",state.toString().str());
							} else {
								trl->setStrategy(strategy);
								trl->saveState();
								req.sendResponse("application/json","true",202);
							}
						}
					}

				}


			}
		}
	} catch (std::exception &e) {
		req.sendErrorPage(500, StrViewA(), e.what());
	}


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
		return std::move(curVal);
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
void WebCfg::State::applyConfig(SharedObject<Traders>  &st) {
	auto t = st.lock();
	t->rpt.lock()->clear();
	auto data = config->load();
	init(data);
	for (auto &&n :traderNames) {
		t->removeTrader(n, !data["traders"][n].defined());
	}

	t->wcfg.walletDB.lock()->clear();
	traderNames.clear();
	t->stockSelector.eraseSubaccounts();
	t->rpt.lock()->clear();

	t->initExternalAssets(data["ext_assets"]);

	for (Value v: data["traders"]) {
		try {
			MTrader_Config cfg;
			cfg.loadConfig(v, t->test);
			t->addTrader(cfg,v.getKey());
			traderNames.push_back(v.getKey());
		} catch (std::exception &e) {
			logError("Failed to initialized trader $1 - $2", v.getKey(), e.what());
		}
	}

	Value bc = data["brokers"];
	broker_config = bc;
	t->stockSelector.forEachStock([&](std::string_view name, const PStockApi &api) {
		Value b = bc[name];
		if (b.defined()) {
			IBrokerControl *eapi = dynamic_cast<IBrokerControl *>(api.get());
			if (eapi) {
				eapi->restoreSettings(b);
			}
		}
	});

	Value newInterval = data["report_interval"];
	if (newInterval.defined()) {
		t->rpt.lock()->setInterval(newInterval.getUInt());
	}
	news_tm = data["news_tm"];
	if (!news_tm.defined()) {
		news_tm = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
}

void WebCfg::State::setAdminAuth(const std::string_view &auth) {
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

bool WebCfg::reqLogout(simpleServer::HTTPRequest req, bool commit) {

	auto hdr = req["Authorization"];
	auto hdr_splt = hdr.split(" ");
	hdr_splt();
	StrViewA cred = hdr_splt();
	auto credobj = AuthUserList::decodeBasicAuth(cred);
	if (commit) {
		if (state.lock()->logout_commit(std::move(credobj.first)))
			auth.genError(req);
		else
			req.sendResponse("text/plain","");
	} else {
		state.lock()->logout_user(std::move(credobj.first));
		std::string rndstr;
		std::time_t t = std::time(nullptr);
		rndstr = "?";
		ondra_shared::unsignedToString(t,[&](char c){rndstr.push_back(c);},16,8);
		req.redirect(strCommand[logout_commit].data()+rndstr,Redirect::temporary_GET);
	}

	return true;
}

bool WebCfg::reqStop(simpleServer::HTTPRequest req)  {
	if (!req.allowMethods({"POST"})) return true;

	auto trl = trlist.lock();


	HTTPResponse hdr(200);
	hdr.contentType("application/json");

	for (auto &&x: *trl) {
		auto t = x.second;
		t.lock()->stop();
	};
	trl->resetBrokers();
	trl->enumTraders([&](const auto &inf)mutable{
		auto tr = inf.second;
		tr.lock()->perform(true);
	});
	trl->rpt.lock()->genReport();
	trl->resetBrokers();
	req.sendResponse(std::move(hdr), "true");


	return true;
}

void WebCfg::State::logout_user(std::string &&user) {
	logout_users.insert(std::move(user));
}

bool WebCfg::State::logout_commit(std::string &&user) {
	if (logout_users.find(user) == logout_users.end()) {
		return false;
	} else {
		logout_users.erase(user);
		return true;
	}
}

bool WebCfg::reqEditor(simpleServer::HTTPRequest req)  {
	if (!req.allowMethods({"POST"})) return true;
	req.readBodyAsync(10000,[trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
			try {

				Value data = Value::fromString(map_bin2str(req.getUserBuffer()));
				Value broker = data["broker"];
				Value trader = data["trader"];
				Value symb = data["pair"];
				std::string p;
				std::size_t uid;
				bool exists = false;
				bool need_initial_reset = true;

				auto walletDB = trlist.lock_shared()->wcfg.walletDB;
				auto extBal = trlist.lock_shared()->wcfg.externalBalance;


				trlist.lock()->stockSelector.checkBrokerSubaccount(broker.getString());
				auto tr = trlist.lock_shared()->find(trader.toString().str());
				auto trl = tr.lock();
				PStockApi api;
				IStockApi::MarketInfo minfo;
				if (tr == nullptr) {
					api = trlist.lock_shared()->stockSelector.getStock(broker.toString().str());
					if (api == nullptr) {
						return req.sendErrorPage(410);
					}
					minfo = api->getMarketInfo(symb.getString());
					uid = 0;
					exists=false;
				} else {
					trl->init();
					api = trl->getBroker();
					minfo = trl->getMarketInfo();
					uid = trl->getUID();
					exists = true;
					need_initial_reset = trl->isInitialResetRequired();
				}
				if (api == nullptr) {
					return req.sendErrorPage(404);
				}
				if (tr && !symb.hasValue()) {
					p = trl->getConfig().pairsymb;
				} else {
					p = symb.toString().str();
				}

				api->reset();
				auto binfo = api->getBrokerInfo();


				Value strategy;
				Value position;
				Value tradeCnt;
				Value enter_price;
				Value enter_price_pos;
				Value costs;
				Value rpnl;
				if (tr) {
					Strategy stratobj=trl->getStrategy();
					strategy = stratobj.dumpStatePretty(trl->getMarketInfo());
					auto trades = trl->getTrades();
					auto assBal = trl->getPosition();
					if (assBal.has_value()) position =*assBal;
					tradeCnt = trades.size();
					enter_price = trl->getEnterPrice();
					costs = trl->getCosts();
					enter_price_pos = trl->getEnterPricePos();
					rpnl = trl->getRPnL();
				}
				std::optional<double> internalBalance, internalCurrencyBalance;
				if (trl) {
					internalBalance = trl->getInternalBalance();
					internalCurrencyBalance = trl->getInternalCurrencyBalance();
				}
				Object result;
				result.set("broker",Object({
						{"name", binfo.name},
						{"exchangeName", binfo.exchangeName},
						{"version", binfo.version},
						{"settings", binfo.settings},
						{"trading_enabled", binfo.trading_enabled}}));
				Value pair = getPairInfo(api, p, internalBalance, internalCurrencyBalance);
				auto alloc = walletDB.lock_shared()->query(WalletDB::KeyQuery(broker.getString(),minfo.wallet_id,minfo.currency_symbol,uid));
				result.set("pair", pair);
				result.set("allocations", Object({

						{"asset",walletDB.lock_shared()->adjAssets(
								WalletDB::KeyQuery(
													broker.getString(),minfo.wallet_id,minfo.asset_symbol,uid
												),pair["asset_balance"].getNumber())},
						{"allocated",alloc.thisTrader},
						{"unavailable",alloc.otherTraders}}));
				auto extBalL = extBal.lock_shared();
				result.set("ext_ass", Object({
						{"currency", extBalL->get(broker.getString(), minfo.wallet_id, minfo.currency_symbol)},
						{"assets", extBalL->get(broker.getString(), minfo.wallet_id, minfo.asset_symbol)}}));

				result.set("orders", getOpenOrders(api, p));
				result.set("strategy", strategy);
				result.set("position", position);
				result.set("enter_price", enter_price);
				result.set("enter_price_pos", enter_price_pos);
				result.set("rpnl", rpnl);
				result.set("costs", costs);
				result.set("accumulation", trl == nullptr?0.0:trl->getAccumulated());
				result.set("trades", tradeCnt);
				result.set("exists", exists);
				result.set("need_initial_reset",need_initial_reset);



				req.sendResponse("application/json", Value(result).stringify().str());
			} catch (std::exception &e) {
				req.sendErrorPage(500, StrViewA(), e.what());
			}
		});
	return true;
}

bool WebCfg::reqLogin(simpleServer::HTTPRequest req)  {
	req.redirect("../index.html", simpleServer::Redirect::temporary_repeat);
	return true;
}

void WebCfg::State::setBrokerConfig(const std::string_view &name, json::Value config) {
	broker_config = broker_config.getValueOrDefault(Value(json::object)).replace(name,config);
}

static Value btevent_no_event;
static Value btevent_margin_call("margin_call");
static Value btevent_liquidation("liquidation");
static Value btevent_no_balance("no_balance");
static Value btevent_error("error");
static Value btevent_accept_loss("accept_loss");

bool WebCfg::reqBacktest(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest)  {
	if (rest.startsWith("v2/")) return reqBacktest_v2(req, rest.substr(3));
	if (!req.allowMethods({"POST","DELETE"})) return true;
	if (req.getMethod() == "DELETE") {
		auto lkst = state.lock();
		lkst->backtest_cache.clear();
		lkst->prices_cache.clear();
		lkst->spread_cache.clear();
		req.sendResponse("application/json","true");
		return true;
	} else  {
		req.readBodyAsync(50000,[&trlist = this->trlist,state =  this->state](simpleServer::HTTPRequest req)mutable{
			try {
				Value orgdata = Value::fromString(map_bin2str(req.getUserBuffer()));
				Value id = orgdata["id"];

				Value reverse=orgdata["reverse"];
				Value invert=orgdata["invert"];


				auto process=[=](Traders &trlist, const BacktestCache::Subj &trades, bool inv, bool rev) {

					Value data = orgdata;
					Value config = data["config"];
					Value init_pos = data["init_pos"];
					Value balance = data["balance"];
					Value init_price = data["init_price"];
					Value fill_atprice= data["fill_atprice"];
					Value negbal= data["neg_bal"];
					Value spend= data["spend"];

					std::uint64_t start_date=data["start_date"].getUIntLong();

					MTrader_Config mconfig;
					mconfig.loadConfig(config,false);
					std::optional<double> m_init_pos;
					if (init_pos.hasValue()) m_init_pos = init_pos.getNumber();

					auto piter = trades.prices.begin();
					auto pend = trades.prices.end();
					double mlt = 1.0;
					double avg = std::accumulate(trades.prices.begin(), trades.prices.end(),0.0,[](double a, const BTPrice &b){return a + b.price;})/trades.prices.size();
					double ip = init_price.getNumber();
					double fv = trades.prices.empty()?ip:trades.prices[rev?trades.prices.size()-1:0].price;
					if (ip && !trades.prices.empty()) {
						if (inv) fv = 2*avg - fv;
						if (trades.minfo.invert_price) {
							mlt = (1.0/ip)/fv;
						} else {
							mlt = ip/fv;
						}
						fv = fv * mlt;
					}

					BTPriceSource source;
					if (rev) {
						auto priter = trades.prices.rbegin();

						source = [&, priter]() mutable {
							std::optional<BTPrice> x;
							while (piter != pend && piter->time < start_date) {++piter; ++priter;}
							if (piter != pend) {
								x=BTPrice {piter->time,priter->price*mlt};
								++piter;
								++priter;
							};
							return x;
						};


					} else {
						source = [&]{
							std::optional<BTPrice> x;
							while (piter != pend && piter->time < start_date) ++piter;
							if (piter != pend) {
								x=BTPrice {piter->time,piter->price*mlt};
								++piter;
							};
							return x;
						};
					}

					if (inv) {
						source = [src = std::move(source),fv](){
							auto r = src();
							if (r.has_value()) r->price = fv*fv/r->price;
							return r;
						};
					}

					BTTrades rs = backtest_cycle(mconfig,std::move(source),
							trades.minfo,m_init_pos, balance.getNumber(), negbal.getBool(), spend.getBool());



					Value result (json::array, rs.begin(), rs.end(), [](const BTTrade &x) {
						Value event;
						switch (x.event) {
						default: event = btevent_no_event;break;
						case BTEvent::accept_loss: event = btevent_accept_loss;break;
						case BTEvent::liquidation: event = btevent_liquidation;break;
						case BTEvent::margin_call: event = btevent_margin_call;break;
						case BTEvent::no_balance: event = btevent_no_balance;break;
						case BTEvent::error: event = btevent_error;break;
						}
						return Object({
								{"np",x.neutral_price},
								{"op",x.open_price},
								{"na",x.norm_accum},
								{"npl",x.norm_profit},
								{"npla",x.norm_profit_total},
								{"pl",x.pl},
								{"ps",x.pos},
								{"pr",x.price},
								{"tm",x.time},
								{"info",x.info},
								{"sz",x.size},
								{"event", event}
						});
					});
					String resstr = result.toString();
					req.sendResponse("application/json",resstr.str());
				};



				auto lkst = state.lock_shared();
				if (lkst->backtest_cache.available(id.toString().str())) {
					auto t = lkst->backtest_cache.getSubject();
					bool inv = t.inverted != invert.getBool();
					bool rev = t.reversed != reverse.getBool();
					process(*trlist.lock(), t, inv, rev);
				} else {
					lkst.release();
						try {
							auto tr = trlist.lock_shared()->find(id.getString()).lock_shared();
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
							trs.inverted = false;
							trs.reversed = false;
							tr.release();

							state.lock()->backtest_cache = BacktestCache(trs, id.toString().str());
							process(*trlist.lock(), trs, invert.getBool(), reverse.getBool());
						} catch (std::exception &e) {
							req.sendErrorPage(400,"", e.what());
						}
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

bool WebCfg::reqSpread(simpleServer::HTTPRequest req)  {
	if (!req.allowMethods({"POST"})) return true;
		req.readBodyAsync(50000,[trlist = this->trlist,state =  this->state](simpleServer::HTTPRequest req)mutable{
		try {
			Value args = Value::fromString(json::map_bin2str(req.getUserBuffer()));
			Value id = args["id"];
			auto process = [=](const SpreadCacheItem &data) {


				Value sma = args["sma"];
				Value stdev = args["stdev"];
				Value force_spread = args["force_spread"];
				Value mult = args["mult"];
				Value dynmult_raise = args["raise"];
				Value dynmult_fall = args["fall"];
				Value dynmult_cap= args["cap"];
				Value dynmult_mode = args["mode"];
				Value dynmult_sliding = args["sliding"];
				Value dynmult_mult = args["dyn_mult"];
				Value order2 = args["order2"];
				Value spread_freeze = args["spread_freeze"];

				auto fn = defaultSpreadFunction(sma.getNumber(), stdev.getNumber(), force_spread.getNumber());
				VisSpread vs(fn, {
						{
								dynmult_raise.getValueOrDefault(1.0),
								dynmult_fall.getValueOrDefault(1.0),
								dynmult_cap.getValueOrDefault(100.0),
								strDynmult_mode[dynmult_mode.getValueOrDefault("independent")],
								dynmult_mult.getBool()
						},
						mult.getNumber(),
						order2.getNumber(),
						dynmult_sliding.getBool(),
						spread_freeze.getBool()
				});

				json::Array chart;
				for (const auto &d: data.chart) {
					auto spinfo = vs.point(d.last);
					if (spinfo.valid) {
						double p = d.last;
						if (data.invert_price) {
							p = 1.0/p;
							spinfo = VisSpread::Result{
								true,1.0/spinfo.price,1.0/spinfo.high,1.0/spinfo.low,-spinfo.trade,1.0/spinfo.price2,-spinfo.trade2
							};
						}
						chart.push_back(Value(json::object,{
								Value("p",p),
								Value("x",spinfo.price),
								Value("l",spinfo.low),
								Value("h",spinfo.high),
								Value("s",spinfo.trade),
								Value("t",d.time)
						}));
						if (spinfo.trade2) {
							chart.push_back(Value(json::object,{
									Value("p",p),
									Value("x",spinfo.price2),
									Value("l",spinfo.low),
									Value("h",spinfo.high),
									Value("s",spinfo.trade2),
									Value("t",d.time)
							}));
						}
					}
				}



				Value out (json::object,{Value("chart",chart)});
				req.sendResponse("application/json", out.stringify().str());
			};

			auto lkst = state.lock_shared();
			if (lkst->spread_cache.available(id.toString().str())) {
				auto t = lkst->spread_cache.getSubject();
				process(t);
			} else {
				lkst.release();
				try {
					auto tr = trlist.lock_shared()->find(id.getString()).lock_shared();
					if (tr == nullptr) {
						req.sendErrorPage(404);
						return;
					}
					SpreadCacheItem x;
					x.chart = tr->getChart();
					x.invert_price = tr->getMarketInfo().invert_price;
					tr.release();
					state.lock()->spread_cache= SpreadCache(x, id.toString().str());
					process(x);
				} catch (std::exception &e) {
					req.sendErrorPage(400,"", e.what());
				}

			}


		} catch (std::exception &e) {
			req.sendErrorPage(400,"",e.what());
		}
		});
	return true;
}


bool WebCfg::reqStrategy(simpleServer::HTTPRequest req) {
	if (!req.allowMethods({"POST"})) return true;
	Stream strm = req.getBodyStream();
	Value jreq = Value::parse(strm);

	Value strategy = jreq["strategy"];
	Strategy s = Strategy::create(strategy["type"].getString(), strategy);
	StrViewA trader = jreq["trader"].getString();
	double assets = jreq["assets"].getNumber();
	double currency = jreq["currency"].getNumber();
	double extra_bal = jreq["extra_balance"].getNumber();
	double price = jreq["price"].getNumber();
	double leverage = jreq["leverage"].getNumber();
	bool inverted = jreq["inverted"].getBool();

	IStockApi::MarketInfo minfo{
			"","",0,0,0,0,0,IStockApi::currency,leverage,inverted
		};


	if (!trader.empty()) {
		auto tr = this->trlist.lock_shared()->find(trader);
		if (tr != nullptr) {
			Strategy trs = tr.lock_shared()->getStrategy();
			minfo = tr.lock_shared()->getMarketInfo();
			if (trs.getID() == s.getID()) {
				s.importState(trs.exportState(),minfo);
			}
		}
	}

	auto tk = IStockApi::Ticker{price,price,price,
		static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
				)
	};

	try {
		s.onIdle(minfo, tk ,assets,currency+extra_bal);
	} catch (...) {
		double cur = currency+extra_bal;
		double z = s.calcInitialPosition(minfo, price, assets, cur);
		double df = z - assets;
		if (!minfo.leverage) cur = cur - df * price;
		s.onIdle(minfo, tk ,z,cur);
	}

	auto range = s.calcSafeRange(minfo, assets, currency);
	auto initial = s.calcInitialPosition(minfo, price, assets, currency+extra_bal);
	Value out = Object({
			{"min", inverted?1.0/range.max:range.min},
			{"max", inverted?1.0/range.min:range.max},
			{"initial", (inverted?-1:1)*initial},
	});

	req.sendResponse("application/json", out.toString().str());
	return true;
}

bool WebCfg::reqDumpWallet(simpleServer::HTTPRequest req, ondra_shared::StrViewA vpath) {
	if (!req.allowMethods({"GET"})) return true;

	if (!vpath.empty()) {
		auto s = trlist.lock_shared()->stockSelector.getStock(vpath);
		if (s != nullptr) {
			IBrokerControl *bk = dynamic_cast<IBrokerControl *>(s.get());
			if (bk) {
				try {
					Object wallet;
					IBrokerControl::AllWallets allw = bk->getWallet();
					for (auto &&x: allw) {
						Object bww;
						for (auto &&y: x.wallet) {
							bww.set(y.symbol, y.balance);
						}
						wallet.set(x.walletId, bww);
					}
					HTTPResponse hdrs(200);
					hdrs.cacheFor(60).contentType("application/json");
					Stream stream = req.sendResponse(std::move(hdrs));
					Value(wallet).serialize(stream);
					return true;
				} catch (...) {

				}
			}
			req.sendResponse(HTTPResponse(501).cacheFor(300),"");
		} else {
			req.sendErrorPage(404);
		}
		return true;
	}

	WalletCfg wcfg = trlist.lock_shared()->wcfg;
	auto lkwallet = wcfg.walletDB.lock_shared();
	auto lkcache = wcfg.balanceCache.lock_shared();

	std::unordered_set<json::Value> activeBrokers;

	auto data = lkwallet->getAggregated();
	json::Value allocations (json::array, data.begin(), data.end(), [&](const WalletDB::AggrItem &x) {
		Object mdata;
		mdata.set("broker", x.broker);
		mdata.set("wallet", x.wallet);
		mdata.set("symbol", x.symbol);
		mdata.set("allocated", x.val);
		mdata.set("balance", lkcache->get(x.broker, x.wallet, x.symbol));
		activeBrokers.insert(json::Value(x.broker));
		return json::Value(mdata);
	});


	json::Value jsn = Object({{"entries", Value(json::array, activeBrokers.begin(), activeBrokers.end(),[](Value x){
		return x;
	})},{"wallet", allocations}});
	Stream stream = req.sendResponse("application/json");
	jsn.serialize(stream);
	return true;
}

bool WebCfg::reqBTData(simpleServer::HTTPRequest req) {
	if (!req.allowMethods({"GET"})) return true;

	auto bb = backtest_broker.lock();
	Value res = bb->jsonRequestExchange("symbols",json::Value(),false);
	bb->stop();

	Stream s = req.sendResponse(HTTPResponse(200).contentType("application/json").cacheFor(80000));
	res.serialize(s);
	return true;
}

static double choose_best_step(double v) {
	double s =  std::pow(10,std::floor(std::log10((v/2))));
	if (std::floor(v/s)<5) s/=2;
	return s;
}

bool WebCfg::reqVisStrategy(simpleServer::HTTPRequest req,  simpleServer::QueryParser &qp) {
	if (!req.allowMethods({"GET"})) return true;
	ondra_shared::StrViewA id = qp["id"];
	ondra_shared::StrViewA assets = qp["asset"];
	ondra_shared::StrViewA currency = qp["currency"];
	ondra_shared::StrViewA sprice = qp["price"];
	double bal_a = std::strtod(assets.data,nullptr);
	double bal_c = std::strtod(currency.data,nullptr);
	double price = std::strtod(sprice.data,nullptr);
	auto trader = trlist.lock_shared()->find(id);
	std::ostringstream tmp;
	if (trader == nullptr) {
		req.sendErrorPage(404);
	} else {
		Strategy st(nullptr);
		IStockApi::MarketInfo minfo;
		IStrategy::MinMax range;
		std::vector<double> pt_budget, pt_value;
		{
			auto tr = trader.lock_shared();
			auto trades = tr->getTrades();
			if (!trades.empty()) price = trades.back().price;
			st = tr->getStrategy();
			minfo = tr->getMarketInfo();
			range = st.calcSafeRange(minfo, bal_a, bal_c);
		}

		Stream out = req.sendResponse(simpleServer::HTTPResponse(200)
					.contentType("image/svg+xml").cacheFor(600));
		out << "<?xml version=\"1.0\"?>"
			<< "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">";

		out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"500\">";


		if (st.isValid()) {

			double pprice = price;
			double p_min = range.min;
			double p_max = range.max;
			if (minfo.invert_price) {
				pprice = 1.0/price;
				p_min = 1.0/range.max;
				p_max = 1.0/range.min;
				if (!std::isfinite(p_min)) p_min = 0;
				if (!std::isfinite(p_max)) p_max = pprice*2;
			}

			p_min = std::max(p_min, 0.0);
			p_max = std::min(p_max, pprice*2);
			p_min = std::min(p_min, pprice);

			double v_min = std::numeric_limits<double>::max();
			double v_max = -v_min;

			auto imap_price = [&](double x)-> double {
				double k = p_min + x/800.0*(p_max - p_min);
				return minfo.invert_price?1.0/k:k;
			};


			for (unsigned int i = 0; i <= 400; i++) {
				double p = imap_price(i*2);
				auto pt = st.calcChart(p);
				if (!pt.valid) {
					continue;
				}
				double value = std::abs(pt.position * p);
				if (pt.budget<v_min) v_min = pt.budget;
				if (pt.budget>v_max) v_max = pt.budget;

				pt_budget.push_back(pt.budget);
				pt_value.push_back(value);

			}

			auto cur = st.calcChart(price);
			double curval = std::abs(cur.position * price);
			if (curval < v_min) v_min = curval;
			if (curval > v_max) v_max = curval;



			{
				double d1 = v_max- v_min;
				v_min -= d1*0.07;
				v_max += d1*0.07;
			}


			auto line = [&](double x1, double y1, double x2, double y2, const std::string_view style) {
				tmp.str("");tmp.clear();
				tmp << "<line x1=\"" << x1 << "\" y1=\"" << y1 <<"\" x2=\"" << x2 << "\" y2=\"" << y2 <<"\" style=\"" << style << "\" />";
				out << tmp.str();
			};
			auto point = [&](double x1, double y1, const std::string_view style) {
				tmp.str("");tmp.clear();
				tmp << "<circle cx=\"" << x1 << "\" cy=\"" << y1 <<"\" r=\"4\" style=\"" << style << "\" />";
				out << tmp.str();
			};
			auto label = [&](double x, double y, double number, const std::string_view style) {
				tmp.str("");tmp.clear();
				const char *sfx = "";
				if (std::abs(number)>1000000) {number/=1000000.0;sfx="M";}
				else if (std::abs(number)>1000) {number/=1000.0;sfx="k";}
				tmp << std::fixed << number;
				std::string c = tmp.str();
				if (c.find('.') != c.npos) {
					while (c.back() == '0') c.pop_back();
					if (c.back() == '.') c.pop_back();
				}

				c.append(sfx);
				out << "<text x=\"" << x << "\" y=\"" << y << "\" style=\"" << style << "\">" << c << "</text>";
			};

			auto map_price = [&](double x) -> double{
				return (x - p_min)/(p_max - p_min)*800.0;
			};
			auto map_y = [&](double y)-> double {
				return (v_max - y)/(v_max - v_min)*500.0;
			};

			double zero_y = map_y(0);


			double fx = choose_best_step(p_max - p_min);
			for (double x = std::floor(p_min/fx)*fx; x < p_max; x+=fx) {
				double mx = map_price(x);
				line(mx,0,mx,500,"stroke:#8888; stroke-width: 1px");
				label(mx,500,x,"alignment-baseline: after-edge; font-size: 20px;fill: #CDCDCD");
			}
			fx = choose_best_step(v_max - v_min);
			for (double y = std::floor(v_min/fx)*fx; y < v_max; y+=fx) {
				double my = map_y(y);
				line(0,my,800,my,"stroke:#8888; stroke-width: 1px");
				label(0,my,y,"alignment-baseline: after-edge; font-size: 20px;fill: #CDCDCD");
			}

			line(0,zero_y,800,zero_y,"stroke: #FFFFFF; stroke-width: 1px");



			for (unsigned int i = 1; i < pt_budget.size(); i++) {
				double x = i * 2.0;
				line(x-2.0,map_y(pt_budget[i-1]),x,map_y(pt_budget[i]),"stroke: #FFFFA0; stroke-width: 2px");
				line(x-2.0,map_y(pt_value[i-1]),x,map_y(pt_value[i]),"stroke: #00AA00; stroke-width: 2px");
			}
			double a = map_price(pprice);
			double b = map_y(cur.budget);
			line(a,0,a,500,"stroke: #CDCDCD; stroke-width: 1px");
			line(0,b,800,b,"stroke: #CDCDCD; stroke-width: 1px");
			auto smernice = [&](double x) {
				return (x - price) * cur.position + cur.budget;
			};

			for (int i = -250; i < 250; i+=2) {
				double v1 = map_y(smernice(imap_price(a+i-2)));
				double v2 = map_y(smernice(imap_price(a+i)));
				line(a+i-2, v1, a+i, v2, "stroke: #80FFFF; stroke-width: 3px");
			}

			point(a, map_y(curval), "fill: #00AA00");
			point(a, b, "fill: #FFFFA0");


		}



		out << "</svg>";
	}

	return true;
}

bool WebCfg::reqUtilization(simpleServer::HTTPRequest req,  simpleServer::QueryParser &qp) {
	if (!req.allowMethods({"GET"})) return true;
	HeaderValue v = qp["tm"];
	json::Value res = trlist.lock_shared()->getUtilization(v.getUInt());
	req.sendResponse("application/json", res.stringify().str());
	return true;
}

enum class BTAction {
	upload_file,
	get_file,
	trader_chart,
	trader_minute_chart,
	random_chart,
	historical_chart,
	gen_trades,
	run,
	probe
};


static NamedEnum<BTAction> strBTAction({
	{BTAction::upload_file, "upload"},
	{BTAction::get_file, "get_file"},
	{BTAction::trader_minute_chart,"trader_minute_chart"},
	{BTAction::trader_chart,"trader_chart"},
	{BTAction::random_chart,"random_chart"},
	{BTAction::historical_chart,"historical_chart"},
	{BTAction::gen_trades, "gen_trades"},
	{BTAction::run, "run"},
	{BTAction::probe, "probe"},
});

bool WebCfg::reqBacktest_v2(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) {
	if (!req.allowMethods({"POST","GET"})) return true;
	if (req.getMethod() == "GET") {
		auto storage = state.lock_shared()->backtest_storage;
		json::Value v = storage.lock()->load_data(rest);
		if (v.defined()) {

			auto stream = req->sendResponse("text/csv");
			for (Value val: v) {
				stream.operator <<(val.toString().str()) << "\r\n";
			}
			stream.flush();
			return true;

		} else {
			req.sendErrorPage(404);
			return true;
		}

	} else {
		auto action = strBTAction[rest];
		req.readBodyAsync(upload_limit,[action,
										trlist = this->trlist,
										state =  this->state,
										prices = this->backtest_broker](simpleServer::HTTPRequest req) mutable{
			Value args = Value::fromString(json::map_bin2str(req.getUserBuffer()));
			auto storage = state.lock()->backtest_storage;
			Value response;

			switch (action) {
				case BTAction::upload_file: {
					std::string id = storage.lock()->store_data(args);
					response=Value(json::object, {Value("id",id)});
				}break;
				case BTAction::trader_minute_chart: {
					Value trader = args["trader"];
					auto tr  =trlist.lock()->find(trader.getString());
					if (tr == nullptr) {req.sendErrorPage(404);return;}
					auto chart = tr.lock_shared()->getChart();
					Value chart_data (json::array, chart.begin(), chart.end(), [](const MTrader::ChartItem &itm)->Value{
						return itm.last;
					});
					std::string id = storage.lock()->store_data(chart_data);
					response=Value(json::object, {Value("id",id)});
				}break;
				case BTAction::trader_chart: {
					Value trader = args["trader"];
					auto tr  =trlist.lock()->find(trader.getString());
					if (tr == nullptr) {req.sendErrorPage(404);return;}
					auto trl = tr.lock_shared();
					auto trd = trl->getTrades();
					auto nfo = trl->getMarketInfo();
					Value chart_data (json::array, trd.begin(), trd.end(), [&](const IStatSvc::TradeRecord &itm)->Value{
						if (nfo.invert_price) return {itm.time, 1.0/itm.price};
						else return {itm.time, itm.price};
					});
					std::string id = storage.lock()->store_data(chart_data);
					response=Value(json::object, {Value("id",id)});
				}break;
				case BTAction::historical_chart: {
					Value asset = args["asset"];
					Value currency = args["currency"];
					unsigned int smooth = args["smooth"].getUInt();

					auto from = std::chrono::system_clock::to_time_t(
							std::chrono::system_clock::now()-std::chrono::hours(365*24)
					);
					from = (from/86400)*86400;
					auto btb = prices.lock();
					Value chart_data = btb->jsonRequestExchange("minute", Object({{"asset", asset},{"currency",currency},{"from",from}}));
					if (smooth>1) {
						double accum = chart_data[0].getNumber()*smooth;
						Value smth_data (json::array,chart_data.begin(), chart_data.end(),[&](const Value &d){
							accum -= accum / smooth;
							accum += d.getNumber();
							return accum/smooth;
						});
						chart_data = smth_data;
					}
					std::string id = storage.lock()->store_data(chart_data);
					response=Value(json::object, {Value("id",id)});
				}break;
				case BTAction::random_chart: {

					std::size_t seed = args["seed"].getUInt();
					double volatility = args["volatility"].getValueOrDefault(0.1);
					double noise = args["noise"].getValueOrDefault(0.0);
					std::vector<double> chart;
					generate_random_chart(volatility*0.01, noise*0.01, 525600, seed, chart);
					Value chart_data (json::array, chart.begin(), chart.end(), [](double &itm)->Value{return itm;});
					std::string id = storage.lock()->store_data(chart_data);
					response=Value(json::object, {Value("id",id)});
				}break;
				case BTAction::get_file: {
					Value source = args["source"];
					json::Value v = storage.lock()->load_data(source.getString());
					if (v.defined()) {
						response = v;
					} else {
						req.sendErrorPage(410);
						return;
					}
				}break;
				case BTAction::gen_trades: {
					Value source = args["source"];
					Value sma = args["sma"];
					Value stdev = args["stdev"];
					Value force_spread = args["force_spread"];
					Value mult = args["mult"];
					Value dynmult_raise = args["raise"];
					Value dynmult_fall = args["fall"];
					Value dynmult_cap = args["cap"];
					Value dynmult_mode = args["mode"];
					Value dynmult_sliding = args["sliding"];
					Value dynmult_mult = args["dyn_mult"];
					Value reverse=args["reverse"];
					Value invert=args["invert"];
					Value ifutures=args["ifutures"];
					Value order2 =args["order2"];
					Value spread_freeze = args["spread_freeze"];
					Value offset = args["offset"];
					Value limit = args["limit"];
					Value begin_time = args["begin_time"];

					Value srcminute = storage.lock()->load_data(source.getString());
					if (!srcminute.defined()) {
						req.sendErrorPage(410);
						return;
					}

					auto fn = defaultSpreadFunction(sma.getNumber(), stdev.getNumber(), force_spread.getNumber());
					VisSpread spreadCalc(fn,{
						{
								dynmult_raise.getValueOrDefault(1.0),
								dynmult_fall.getValueOrDefault(1.0),
								dynmult_cap.getValueOrDefault(100.0),
								strDynmult_mode[dynmult_mode.getValueOrDefault("independent")],
								dynmult_mult.getBool()
						},
						mult.getNumber(),
						order2.getNumber(),
						dynmult_sliding.getBool(),
						spread_freeze.getBool()
					});
					if (reverse.getBool()) {
						srcminute = srcminute.reverse();
					}
					bool inv = invert.getBool();
					bool ifut = ifutures.getBool();
					double init = 0;
					std::vector<BTPrice> out;
					out.reserve(srcminute.size());
					std::uint64_t t = !begin_time.defined()?std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::system_clock::now() - std::chrono::minutes(srcminute.size())).time_since_epoch()).count()
								:begin_time.getUIntLong();
					BTPrice tmp;
					BTPrice *last = &tmp;
					std::size_t ofs = offset.getUInt();
					std::size_t lim = std::min<std::size_t>(limit.defined()?limit.getUInt()+ofs:static_cast<std::size_t>(-1),srcminute.size());
					for (std::size_t pos = ofs; pos < lim;++pos) {
						const auto &itm =  srcminute[pos];
						double w = itm.getNumber();
						if (inv) {
							if (init == 0) init = pow2(w);
							w = init/w;
						}
						double v = w;
						if (ifut) {
							v = 1.0/v;
						}
						auto res = spreadCalc.point(v);
						if (res.trade && res.valid) {
							double p = ifut?1.0/res.price:res.price;
							out.push_back({t, p,p,p});
							last = &out.back();
							if (res.trade2) {
								double p = ifut?1.0/res.price2:res.price2;
								out.push_back({t, p,p,p});
								last = &out.back();
							}
						} else if (w<last->pmin) {
							last->pmin = w;
						} else if (w>last->pmax) {
							last->pmax = w;
						}

						t+=60000;
					}
					Value chart_data(json::array, out.begin(), out.end(), [](const BTPrice &bt)->json::Value{
						return {bt.time, bt.price, {bt.pmin, bt.pmax}};
					});
					std::string id = storage.lock()->store_data(chart_data);
					response=Value(json::object, {
							Value("id",id),
							Value("samples",srcminute.size()),
							Value("trades",chart_data.size())
					});
				}break;
				case BTAction::probe:
				case BTAction::run: {

					Value minfo_val = args["minfo"];
					Value source = args["source"];

					Value reverse=args["reverse"];
					Value invert=args["invert"];

					Value config = args["config"];
					Value init_pos = args["init_pos"];
					Value balance = args["balance"];
					Value init_price = args["init_price"];
					Value fill_atprice= args["fill_atprice"];
					Value negbal= args["neg_bal"];
					Value spend= args["spend"];

					bool rev = reverse.getBool();
					bool inv = invert.getBool();

					if (!minfo_val.defined()) {
						req.sendErrorPage(400,"Missing minfo");return;
					}
					auto minfo = IStockApi::MarketInfo::fromJSON(minfo_val);

					std::uint64_t start_date=args["start_date"].getUIntLong();

					MTrader_Config mconfig;
					mconfig.loadConfig(config,false);
					std::optional<double> m_init_pos;
					if (init_pos.hasValue()) m_init_pos = init_pos.getNumber();

					Value jtrades = storage.lock()->load_data(source.getString());
					if (!jtrades.defined()) {
						req.sendErrorPage(410);
						return;
					}


					std::vector<BTPrice> trades;
					trades.reserve(jtrades.size());


					for (Value x: jtrades) {
						std::uint64_t tm = x[0].getUIntLong();
						if (tm >= start_date) {
							Value r = x[2];
							bool hasr = r.type() == json::array;
							double p = x[1].getNumber();
							double pmin = hasr?x[2][0].getNumber():p;
							double pmax = hasr?x[2][1].getNumber():p;
							trades.push_back({tm, p, pmin,pmax});
						}
					}

					if (rev) {
						for (std::size_t i = 0, cnt = trades.size(); i<cnt/2; i++) {
							std::swap(trades[i].price, trades[cnt-i-1].price);
						}
					}

					double mlt = 1.0;
					double avg = std::accumulate(trades.begin(), trades.end(),0.0,[](double a, const BTPrice &b){
						return a + b.price;
					})/trades.size();

					double ip = init_price.getNumber();
					double fv = trades.empty()?ip:trades[0].price;
					if (ip && !trades.empty()) {
						if (inv) fv = 2*avg - fv;
						mlt = ip/fv;
						fv = fv * mlt;
					}

					for (auto &x: trades) {
						x.price *= mlt;
						x.pmin *= mlt;
						x.pmax *= mlt;
					}

					if (inv) {
						for (auto &x: trades) {
							x.price = pow2(fv)/x.price;
							double tmp = pow2(fv)/x.pmin;
							x.pmin = pow2(fv)/x.pmax;
							x.pmax = tmp;
						}
					}
					if (minfo.invert_price) {
						for (auto &x: trades) {
							x.price = 1.0/x.price;
							double tmp = 1.0/x.pmin;
							x.pmin = 1.0/x.pmax;
							x.pmax = tmp;
						}
					}




					BTTrades rs = backtest_cycle(mconfig,
							[iter = trades.begin(), end = trades.end()]() mutable {
						if (iter == end) return std::optional<BTPrice>();
						else return std::optional<BTPrice>(*iter++);
					},minfo,m_init_pos, balance.getNumber(), negbal.getBool(), spend.getBool());

					if (action == BTAction::run) {

						ACB acb(0,0);
						double prev_open = 0;
						Value result (json::array, rs.begin(), rs.end(), [&](const BTTrade &x) {
							Value event;
							double open;
							if (minfo.invert_price) {
								acb = acb(1.0/x.price, -x.size);
								open = 1.0/acb.getOpen();
							} else {
								acb = acb(x.price, x.size);
								open = acb.getOpen();
							}
							if (acb.getPos() == 0) {
								open = prev_open;
							} else {
								prev_open = open;
							}

							switch (x.event) {
							default: event = btevent_no_event;break;
							case BTEvent::accept_loss: event = btevent_accept_loss;break;
							case BTEvent::liquidation: event = btevent_liquidation;break;
							case BTEvent::margin_call: event = btevent_margin_call;break;
							case BTEvent::no_balance: event = btevent_no_balance;break;
							case BTEvent::error: event = btevent_error;break;
							}
							return Object({
									{"np",x.neutral_price},
									{"op",open},
									{"rpnl",acb.getRPnL()},
									{"upnl",acb.getUPnL(x.price)},
									{"na",x.norm_accum},
									{"npl",x.norm_profit},
									{"npla",x.norm_profit_total},
									{"pl",x.pl},
									{"ps",x.pos},
									{"pr",x.price},
									{"tm",x.time},
									{"bal",x.bal},
									{"ubal",x.unspend_balance},
									{"info",x.info},
									{"sz",x.size},
									{"event", event}
							});
						});

						response = result;
					} else {
						std::size_t accept_loss = 0, liquidation=0,margin_call=0,no_balance=0,error=0,alerts=0;
						for (const auto &item: rs) {
							switch (item.event) {
								case BTEvent::accept_loss: ++accept_loss;break;
								case BTEvent::liquidation: ++liquidation;break;
								case BTEvent::margin_call: ++margin_call;break;
								case BTEvent::no_balance: ++no_balance;break;
								case BTEvent::error: ++error;break;
								default:break;
							}
							if (item.size == 0) ++alerts;
						}

						double bal = 1;
						double pl = 0;
						double npl = 0;
						double na = 0;

						if (!rs.empty()) {
							bal = balance.getNumber();
							if (minfo.leverage==0) {
								bal += init_pos.getNumber()*rs[0].price;
							}
							pl = rs.back().pl;
							npl = rs.back().norm_profit;
							na = rs.back().norm_accum;
						}

						response = json::Object {
							{"events",json::Object {
								{"accept_loss",accept_loss},
								{"liquidation",liquidation},
								{"margin_call",margin_call},
								{"no_balance",no_balance},
								{"error",error},
								{"alerts",alerts},
							}},
							{"pl",pl},
							{"npl",npl},
							{"na",na},
							{"pc_pl",pl/bal*100.0},
							{"pc_npl",npl/bal*100.0}
						};
					}



				}break;
				default:
					req.sendErrorPage(404);
					return;
			}

			auto stream = req.sendResponse("application/json");
			response.serialize(stream);
		});
	}
	return true;
}


void WebCfg::State::initProgress(std::size_t i) {
	progress_map.emplace(i,std::pair(0,false));
}
bool WebCfg::State::setProgress(std::size_t i, json::Value v) {
	auto iter = progress_map.find(i);
	if (iter == progress_map.end()) return false;
	iter->second.first = v;
	return !iter->second.second;
}
void WebCfg::State::clearProgress(std::size_t i) {
	progress_map.erase(i);
}
json::Value WebCfg::State::getProgress(std::size_t i) const {
	auto iter = progress_map.find(i);
	if (iter == progress_map.end()) return json::Value();
	return iter->second.first;
}

void WebCfg::State::stopProgress(std::size_t i)  {
	auto iter = progress_map.find(i);
	if (iter != progress_map.end()) iter->second.second = true;
}

WebCfg::Progress::Progress(const SharedObject<State> &state, std::size_t id)
	:state(state),id(id) {
	this->state.lock()->initProgress(id);
}
WebCfg::Progress::Progress(Progress &&s)
	:state(std::move(s.state)),id(s.id) {
	s.id = -1;
}
WebCfg::Progress::Progress(const Progress &) {
	throw std::runtime_error("Progress - Can't handle copy");
}


WebCfg::Progress::~Progress() {
	if (state != nullptr) state.lock()->clearProgress(id);

}
bool WebCfg::Progress::set(json::Value amount) {
	return state.lock()->setProgress(id, amount);
}

bool WebCfg::reqProgress(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest) {
	if (!req.allowMethods({"GET","DELETE"})) return true;
	std::size_t id = 0;
	for (char c: rest) id = id * 10 + (c - '0');
	if (req.getMethod() == "DELETE") {
		state.lock()->stopProgress(id);
		req.sendErrorPage(202);
	} else {
		json::Value st = state.lock()->getProgress(id);
		if (st.defined()) {
			req.sendResponse(simpleServer::HTTPResponse(200)
			.contentType("application/json")
			.disableCache(),st.stringify().str());
		} else {
			req.sendResponse(simpleServer::HTTPResponse(204)
			.disableCache(),"");
		}
	}
	return true;
}

struct WebCfg::DataDownloaderTask {
	std::string pair;
	PState state;
	PStockApi api;
	IStockApi::MarketInfo minfo;
	std::shared_ptr<Progress> prg;
	std::string dwnid;
	AsyncProvider async;

	std::vector<IHistoryDataSource::OHLC> tmpVect;
	std::stack<std::vector<IHistoryDataSource::OHLC> > datastack;
	std::uint64_t end_tm ;
	std::uint64_t start_tm;
	std::size_t cnt;
	std::uint64_t n;

	void init();
	void operator()();
	void done();
};

void WebCfg::DataDownloaderTask::init() {
	auto now = std::chrono::system_clock::now();
	end_tm = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	start_tm = end_tm-24ULL*60ULL*60ULL*1000ULL*365ULL;
	cnt = 0;
	n = end_tm;
}


void WebCfg::DataDownloaderTask::operator()() {
	auto bc = dynamic_cast<IHistoryDataSource *>(api.get());
	if (bc && n && prg->set((end_tm - n)/((double)(end_tm-start_tm)/100)) && n > start_tm) {
		try {
			n = bc->downloadMinuteData(
					minfo.asset_symbol,
					minfo.currency_symbol,
					pair, start_tm, n, tmpVect);
		} catch (std::exception &e) {
			prg->set(e.what());
			std::this_thread::sleep_for(std::chrono::seconds(2));
			async.runAsync(std::move(*this));
			return;
		}
		cnt+=tmpVect.size();
		datastack.push(std::move(tmpVect));
		tmpVect.clear();
		async.runAsync(std::move(*this));
	} else {
		done();
	}

}
void WebCfg::DataDownloaderTask::done() {
	tmpVect.reserve(cnt);
	while (!datastack.empty()) {
		const auto &p = datastack.top();
		tmpVect.insert(tmpVect.end(), p.begin(), p.end());
		datastack.pop();
	}
	Value out = json::array;
	if (!tmpVect.empty()) {
		out = Value(json::array, tmpVect.begin(), tmpVect.end(),[&](const auto &ohlc) -> Value {
			double du = ohlc.high-ohlc.open;
			double dw = ohlc.open-ohlc.low;
			if (du > 2*dw) return ohlc.high;
			if (dw > 2*du) return ohlc.low;
			return ohlc.close;
		});
	}
	auto storage = state.lock_shared()->backtest_storage;
	storage.lock()->store_data(out, dwnid);
}


void WebCfg::processBrokerHistory(simpleServer::HTTPRequest req,
				PState state, PStockApi api, ondra_shared::StrViewA pair
) {
	if (!req.allowMethods({"GET","POST"})) return;
	auto bc = dynamic_cast<IHistoryDataSource *>(api.get());
	if (bc == nullptr) {
		req.sendErrorPage(204);
		return;
	}

	auto minfo = api->getMarketInfo(pair);

	if (req.getMethod() == "GET") {

		bool res = bc->areMinuteDataAvailable(minfo.asset_symbol, minfo.currency_symbol);
		if (res) req.sendErrorPage(200);
		else req.sendErrorPage(204);
	} else {
		std::string digest = std::to_string(reinterpret_cast<std::uintptr_t>(api.get()));
		digest.append(pair.data, pair.length);
		std::hash<std::string> hstr;
		auto id = hstr(digest);
		std::string strid = std::to_string(id);
		std::string dwnid = "dwn_"+strid;
		json::Value resp = json::Object{
			{"progress",strid},
			{"data",dwnid}
		};
		std::shared_ptr<Progress> prg;
		bool inp = state.lock_shared()->getProgress(id).defined();
		if (!inp) prg = std::make_shared<Progress>(state, id);
		Stream s = req.sendResponse(simpleServer::HTTPResponse(202)
			.contentType("application/json")
			.disableCache());
		auto async = s.getAsyncProvider();
		resp.serialize(s);
		s.flush();
		if (!inp) {
			DataDownloaderTask task{pair, state, api, minfo, prg, dwnid, async};
			task.init();
			async.runAsync(std::move(task));
		}
	}
}

bool WebCfg::reqNews(simpleServer::HTTPRequest req) {
	if (!state.lock_shared()->isNewsConfigured()) return false;
	if (!req.allowMethods({"GET","POST"})) return true;
	if (req.getMethod() =="GET") {
		auto resp = state.lock_shared()->loadNews(true);
		Stream s = req.sendResponse(simpleServer::HTTPResponse(200)
			.contentType("application/json")
			.disableCache());
		resp.serialize(s);
		s.flush();
		return true;
	} else 	if (req.getMethod() =="POST") {
		try {
			auto dta = Value::parse(req.getBodyStream());
			if (dta.type() == json::number) {
				state.lock()->markNewsRead(dta);
				req.sendErrorPage(202);
				auto rpt = trlist.lock_shared()->rpt;
				rpt.lock()->setNewsMessages(0);
			} else {
				req.sendErrorPage(400);
			}
		} catch (json::ParseError &e) {
			req.sendErrorPage(400);
		}
		return true;
	} else {
		return false;
	}
}

json::Value WebCfg::State::loadNews(bool all) const {
	std::string url = news_url;
	try {
		String tm;
		if (!all && news_tm.defined()) {
			tm = news_tm.toString();
		}
		auto p = url.find("${tm}");
		if (p != url.npos) {
			url = url.substr(0,p).append(tm.str()).append(url.substr(p+5));
		}
			HTTPJson httpc(simpleServer::HttpClient("", simpleServer::newHttpsProvider()),"");
		json::Value v = httpc.GET(url);
		if (all) {
			auto tm = news_tm.getUIntLong();
			v = v.replace("items", v["items"].map([&](json::Value z){
				return z.replace("unread",z["time"].getUIntLong() >= tm);
			}));
		}
		return v;
	} catch (std::exception &e) {
		return Object {
			{"title","Error"},
			{"src","."},
			{"items",{
				Object {
					{"title","Unable to download news: " + url},
					{"body",e.what()},
					{"time",0},
					{"unread",true},
					{"hl",true}
				}
			}}
		};
	}

}

bool WebCfg::State::isNewsConfigured() const {
	return !news_url.empty();
}

void WebCfg::State::markNewsRead(json::Value tm) {
	news_tm = tm;
	auto data = config->load();
	if (data["news_tm"] != tm) {
		data.setItems({
			{"news_tm", tm}
		});
		config->store(data);
	}
}
