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
	{WebCfg::strategy, "strategy"},
	{WebCfg::upload_prices, "upload_prices"},
	{WebCfg::upload_trades, "upload_trades"},
	{WebCfg::wallet, "wallet"},
	{WebCfg::btdata, "btdata"},
	{WebCfg::visstrategy, "visstrategy"},
	{WebCfg::utilization, "utilization"}
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
		case backtest: return reqBacktest(req);
		case spread: return reqSpread(req);
		case strategy: return reqStrategy(req);
		case upload_prices: return reqUploadPrices(req);
		case upload_trades: return reqUploadTrades(req);
		case wallet: return reqDumpWallet(req, rest);
		case btdata: return reqBTData(req);
		case visstrategy: return reqVisStrategy(req, qp);
		case utilization: return reqUtilization(req, qp);
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
		if (!data.defined()) data = Object("revision",0);
		req.sendResponse("application/json",data.stringify());

	} else {


		req.readBodyAsync(upload_limit, [state=this->state,traders=this->trlist,dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
			try {
				auto lkst = state.lock();
				Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
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
				data = data.replace("revision", lkst->write_serial+1);
				data = data.replace("brokers", lkst->broker_config);
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
				req.sendResponse(HTTPResponse(202).contentType("application/json"),data.stringify());


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

	Value res = Object("name", binfo.name)("trading_enabled",
			binfo.trading_enabled)("exchangeName", binfo.exchangeName)(
			"exchangeUrl", url)("version", binfo.version)("subaccounts",binfo.subaccounts);
			return res;
}


bool WebCfg::reqBrokers(simpleServer::HTTPRequest req, ondra_shared::StrViewA rest)  {
	if (rest.empty()) {
		if (!req.allowMethods({"GET"})) return true;
		Array brokers;
		trlist.lock_shared()->stockSelector.forEachStock([&](const std::string_view &name,const PStockApi &){
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
			req.sendResponse("application/json",Value(res).stringify());
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

	if (quote_currency.getString() == "XBT") quote_currency = "BTC";
	if (quote_asset.getString() == "XBT") quote_currency = "BTC";

	Value resp = Object("symbol",pair)("asset_symbol", nfo.asset_symbol)(
			"currency_symbol", nfo.currency_symbol)("fees",
			nfo.fees)("leverage", nfo.leverage)(
			"invert_price", nfo.invert_price)(
			"asset_balance", ab)("currency_balance", cb)(
			"min_size", nfo.min_size)("price",last)
			("quote_currency", quote_currency)
			("quote_asset", quote_asset);
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
					req.sendResponse("application/json", Value(newname).stringify());
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
						req.sendResponse(std::move(hdr), Value(result).stringify());
						return true;
					}
				} catch (...) {

				}
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
						Value resp = getPairInfo(api, p).replace("entries",{"orders", "ticker", "settings"});
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
					}  else if (orders == "info") {
						IStockApi::MarketInfo minfo = api->getMarketInfo(p);
						Value resp = Object
								("asset_step", minfo.asset_step)
								("asset_symbol", minfo.asset_symbol)
								("currency_step", minfo.currency_step)
								("currency_symbol", minfo.currency_symbol)
								("feeScheme", (int)minfo.feeScheme)
								("fees", minfo.fees)
								("invert_price", minfo.invert_price)
								("inverted_symbol", minfo.inverted_symbol)
								("leverage", minfo.leverage)
								("min_size", minfo.min_size)
								("min_volume", minfo.min_volume)
								("private_chart", minfo.private_chart)
								("simulator", minfo.simulator)
								("wallet_id", minfo.wallet_id);
						req.sendResponse(std::move(hdr), resp.stringify());
						return true;
					}  else if (orders == "settings") {

						IBrokerControl *bc = dynamic_cast<IBrokerControl *>(api.get());
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
							else state.lock()->setBrokerConfig(broker_name, res);
							req.sendResponse("application/json", res.stringify(), 202);
							return true;
						}
					}else if (orders == "orders") {
						if (!req.allowMethods( { "GET", "POST", "DELETE" }))
							return true;
						if (req.getMethod() == "GET") {
							Value orders = getOpenOrders(api, p);
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
			res = Object("entries", res);
			req.sendResponse(std::move(hdr), res.stringify());
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
						Value(Object("entries",{"stop","clear_stats","reset","broker","trading","strategy"})).stringify());
				}
			} else {
				auto trl = tr.lock();
				trl->init();
				auto cmd = urlDecode(StrViewA(splt()));
				if (cmd == "clear_stats") {
					if (!req.allowMethods({"POST"})) return true;
					trl->clearStats();
					req.sendResponse(std::move(hdr), "true");
				} else if (cmd == "stop") {
					if (!req.allowMethods({"POST"})) return true;
					trl->stop();
					req.sendResponse(std::move(hdr), "true");
				} else if (cmd == "reset") {
					if (!req.allowMethods({"POST"})) return true;
					trl.release();
					req->readBodyAsync(1000,[tr, hdr=std::move(hdr)](HTTPRequest req) mutable {
						BinaryView data = req.getUserBuffer();
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
					StringView<MTrader::ChartItem> chart(chartx.data(), chartx.size());
					PStockApi broker = trl->getBroker();
					broker->reset();
					if (chart.length>600) chart = chart.substr(chart.length-600);
					out.set("chart", Value(json::array,chart.begin(), chart.end(),[&](auto &&item) {
						return Object("time", item.time)("last",item.last);
					}));
					std::size_t start = chart.empty()?0:chart[0].time;
					auto trades = trl->getTrades();
					out.set("trades", Value(json::array, trades.begin(), trades.end(),[&](auto &&item) {
						if (item.time >= start) return item.toJSON(); else return Value();
					}));
					auto ticker = broker->getTicker(trl->getConfig().pairsymb);
					double stprice = strtod(splt().data,0);
					out.set("ticker", Object("ask", ticker.ask)("bid", ticker.bid)("last", ticker.last)("time", ticker.time));
					out.set("orders", getOpenOrders(broker, trl->getConfig().pairsymb));
					out.set("broker", trl->getConfig().broker);
					std::optional<double> ibalance ;
					if (trl != nullptr) {
						ibalance = trl->getInternalBalance();
					}
					out.set("pair", getPairInfo(broker, trl->getConfig().pairsymb, ibalance));
					if (trl != nullptr) {
						auto strategy = trl->getStrategy();
						double assets = out["pair"]["asset_balance"].getNumber();
						double currencies = out["pair"]["currency_balance"].getNumber();
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
						out.set("strategy",Object("size", (minfo.invert_price?-1:1)*order.size));
					}
					req.sendResponse(std::move(hdr), Value(out).stringify());
				} else if (cmd == "strategy") {
					if (!req.allowMethods({"GET","PUT"})) return true;
					Strategy strategy = trl->getStrategy();
					if (req.getMethod() == "GET") {
						auto st = trl->getMarketStatus();
						json::Value v = strategy.exportState();
						if (trl->getConfig().internal_balance) {
							v = v.replace("internal_balance", Object
									("assets", st.assetBalance)
									("currency", st.currencyBalance)
								);
						}
						req.sendResponse(std::move(hdr), v.stringify());
					} else {
						json::Value v = json::Value::parse(req.getBodyStream());
						strategy.importState(v,trl->getMarketInfo());
						auto st = v["internal_balance"];
						if (st.hasValue()) {
							Value assets = st["assets"];
							Value currency = st["currency"];
							trl->setInternalBalancies(assets.getNumber(), currency.getNumber());
						}
						MTrader::Status mst = trl->getMarketStatus();
						strategy.onIdle(trl->getMarketInfo(), mst.ticker, mst.assetBalance, mst.currencyBalance);
						if (!strategy.isValid()) {
							req.sendErrorPage(409,"","Settings was not accepted");
						} else {
							trl->setStrategy(strategy);
							trl->saveState();
							req.sendResponse("application/json","true",202);
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
void WebCfg::State::applyConfig(SharedObject<Traders>  &st) {
	auto t = st.lock();
	t->rpt.lock()->clear();
	auto data = config->load();
	init(data);
	for (auto &&n :traderNames) {
		t->removeTrader(n, !data["traders"][n].defined());
	}

	t->walletDB.lock()->clear();
	traderNames.clear();
	t->stockSelector.eraseSubaccounts();
	t->rpt.lock()->clear();

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
		req.redirect(strCommand[logout_commit].data+rndstr,Redirect::temporary_GET);
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
	if (state.lock_shared()->upload_progress != -1) {
		req.sendErrorPage(503);
		return true;
	}
	req.readBodyAsync(10000,[trlist = this->trlist,state =  this->state, dispatch = this->dispatch](simpleServer::HTTPRequest req) mutable {
			try {

				Value data = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value broker = data["broker"];
				Value trader = data["trader"];
				Value symb = data["pair"];
				std::string p;
				std::size_t uid;
				bool exists = false;
				bool need_initial_reset = true;


				trlist.lock()->stockSelector.checkBrokerSubaccount(broker.getString());
				auto tr = trlist.lock_shared()->find(trader.toString().str());
				auto trl = tr.lock();
				PStockApi api;
				IStockApi::MarketInfo minfo;
				if (tr == nullptr) {
					api = trlist.lock_shared()->stockSelector.getStock(broker.toString().str());
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

				auto walletDB = trlist.lock_shared()->walletDB;


				api->reset();
				auto binfo = api->getBrokerInfo();


				Value strategy;
				Value position;
				Value tradeCnt;
				if (tr) {
					Strategy stratobj=trl->getStrategy();
					strategy = stratobj.dumpStatePretty(trl->getMarketInfo());
					auto trades = trl->getTrades();
					auto pos = std::accumulate(trades.begin(), trades.end(),0.0,[&](
							auto &&a, auto &&b
					) {
						return a + b.eff_size;
					});
					position = pos;
					tradeCnt = trades.size();
				}
				std::optional<double> internalBalance, internalCurrencyBalance;
				if (trl) {
					internalBalance = trl->getInternalBalance();
					internalCurrencyBalance = trl->getInternalCurrencyBalance();
				}
				Object result;
				result.set("broker",Object
						("name", binfo.name)
						("exchangeName", binfo.exchangeName)
						("version", binfo.version)
						("settings", binfo.settings)
						("trading_enabled", binfo.trading_enabled));
				Value pair = getPairInfo(api, p, internalBalance, internalCurrencyBalance);
				double avail_cur = walletDB.lock_shared()->adjBalance(WalletDB::KeyQuery(
						broker.getString(),minfo.wallet_id,minfo.currency_symbol,uid
					),pair["currency_balance"].getNumber());
				result.set("pair", pair);
				result.set("available_balance", Object

						("asset",walletDB.lock_shared()->adjBalance(
								WalletDB::KeyQuery(
													broker.getString(),minfo.wallet_id,minfo.asset_symbol,uid
												),pair["asset_balance"].getNumber()))
						("currency",avail_cur)
						("budget",walletDB.lock_shared()->query(WalletDB::KeyQuery(
												broker.getString(),minfo.wallet_id,minfo.currency_symbol,uid
											)).otherTraders));
				result.set("orders", getOpenOrders(api, p));
				result.set("strategy", strategy);
				result.set("position", position);
				result.set("trades", tradeCnt);
				result.set("exists", exists);
				result.set("need_initial_reset",need_initial_reset);



				req.sendResponse("application/json", Value(result).stringify());
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

void WebCfg::State::setBrokerConfig(json::StrViewA name, json::Value config) {
	broker_config = broker_config.getValueOrDefault(Value(json::object)).replace(name,config);
}

static Value btevent_no_event;
static Value btevent_margin_call("margin_call");
static Value btevent_liquidation("liquidation");
static Value btevent_no_balance("no_balance");
static Value btevent_error("error");
static Value btevent_accept_loss("accept_loss");

bool WebCfg::reqBacktest(simpleServer::HTTPRequest req)  {
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
				Value orgdata = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
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
						source = [src = std::move(source),avg,fv](){
							auto r = src();
							if (r.has_value()) r->price = fv*fv/r->price;
							return r;
						};
					}

					BTTrades rs = backtest_cycle(mconfig,std::move(source),
							trades.minfo,m_init_pos, balance.getNumber(), negbal.getBool());



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
								("info",x.info)
								("sz",x.size)
								("event", event);
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
			Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
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

				auto res = MTrader::visualizeSpread(IterFn(
						data.chart.begin(),data.chart.end()),
						sma.getNumber(),
						stdev.getNumber(),
						force_spread.getNumber(),
						mult.getNumber(),
						dynmult_raise.getValueOrDefault(1.0),
						dynmult_fall.getValueOrDefault(1.0),
						dynmult_cap.getValueOrDefault(100.0),
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

bool WebCfg::reqUploadPrices(simpleServer::HTTPRequest req)  {
	if (!req.allowMethods({"POST","GET","DELETE"})) return true;
	if (req.getMethod() == "GET") {
		req.sendResponse("application/json",Value(state.lock_shared()->upload_progress).stringify());
		return true;
	} else  if (req.getMethod() == "DELETE") {
			state.lock()->cancel_upload = true;
			req.sendResponse("application/json",Value(state.lock_shared()->upload_progress).stringify());
			return true;
	} else {
	req.readBodyAsync(upload_limit,[&trlist = this->trlist,state =  this->state, dispatch = this->dispatch, btbroker = this->backtest_broker](simpleServer::HTTPRequest req)mutable{
		try {
			Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
			Value id = args["id"];
			Value prices = args["prices"];



			if (prices.getString() == "internal") {
				auto lkst = state.lock();
				lkst->prices_cache.clear();
				lkst->upload_progress = 0;
			} else if (prices.getString() == "update") {
				auto lkst = state.lock();
				lkst->upload_progress = 0;

			} else {

				auto trp = trlist.lock_shared()->find(id.getString());
				if (trp.lock_shared()->need_init()) trp.lock()->init();
				auto tr = trp.lock_shared();
				if (tr == nullptr) {
					req.sendErrorPage(404);
					return;
				}

				IStockApi::MarketInfo minfo = tr->getMarketInfo();
				std::vector<double> chart;

				if (prices.getString() == "random") {


					std::size_t seed = args["seed"].getUInt();
					double volatility = args["volatility"].getValueOrDefault(0.1);
					double noise = args["noise"].getValueOrDefault(0.0);
					generate_random_chart(volatility*0.01, noise*0.01, 525600, seed, chart);

				} else if (prices.getString() == "history_broker") {
					Value asset = args["asset"];
					Value currency = args["currency"];
					auto from = std::chrono::system_clock::to_time_t(
							std::chrono::system_clock::now()-std::chrono::hours(365*24)
					);
					from = (from/86400)*86400;
					auto btb = btbroker.lock();
					Value data = btb->jsonRequestExchange("minute", Object("asset", asset)("currency",currency)("from",from));
					std::transform(data.begin(), data.end(), std::back_inserter(chart),[&](Value itm){
						double p = itm.getNumber();
						if (minfo.invert_price) p = 1.0/p;
						return p;
					});
				} else {

					std::transform(prices.begin(), prices.end(), std::back_inserter(chart),[&](Value itm){
						double p = itm.getNumber();
						if (minfo.invert_price) p = 1.0/p;
						return p;
					});

				}


				tr.release();
				auto lkst = state.lock();
				lkst->prices_cache = PricesCache(chart, id.toString().str());
				lkst->upload_progress = 0;
			}
			req.sendResponse("application/json", "0");
			req = nullptr;
			dispatch ([trlist, state, args]() mutable {
				generateTrades(trlist, state, args);
			});
		} catch (std::exception &e) {
			req.sendErrorPage(400,"",e.what());
		}
	});
	}
	return true;
}
bool WebCfg::reqUploadTrades(simpleServer::HTTPRequest req)  {
	if (!req.allowMethods({"POST"})) return true;
	req.readBodyAsync(upload_limit,[&trlist = this->trlist,state =  this->state](simpleServer::HTTPRequest req)mutable{
			try {
				Value args = Value::fromString(StrViewA(BinaryView(req.getUserBuffer())));
				Value id = args["id"];
				Value prices = args["prices"];
				auto trp = trlist.lock_shared()->find(id.getString());
				if (trp == nullptr) {
						req.sendErrorPage(404);
						return;
				}
				if (trp.lock_shared()->need_init()) trp.lock()->init();
				auto tr = trp.lock_shared();
				IStockApi::MarketInfo minfo = tr->getMarketInfo();
				BacktestCacheSubj bt;
				std::transform(prices.begin(), prices.end(), std::back_inserter(bt.prices), [&](const Value &itm) {
						std::uint64_t tm = itm[0].getUIntLong();
						double p = itm[1].getNumber();
						if (minfo.invert_price) p = 1.0/p;
						return BTPrice{tm, p};
				});
				bt.minfo = minfo;
				bt.reversed = false;
				bt.inverted = false;
				tr.release();
				auto lkst = state.lock();
				lkst->upload_progress = -1;
				lkst->backtest_cache = BacktestCache(bt, id.toString().str());
				req.sendResponse("application/json", "true");
			} catch (std::exception &e) {
				req.sendErrorPage(400,"",e.what());
			}
		});
	return true;
}
bool WebCfg::generateTrades(const SharedObject<Traders> &trlist, PState state, json::Value args) {
	try {
		Value id = args["id"];
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


		auto lkst = state.lock();

		auto tr = trlist.lock_shared()->find(id.getString()).lock_shared();
		if (tr == nullptr) {
			lkst->upload_progress = -1;
			return false;
		}

		bool rev = reverse.getBool();

		std::function<std::optional<MTrader::ChartItem>()> source;
		lkst->upload_progress = 0;
		if (!lkst->prices_cache.available(id.getString())) {
			auto chart = tr->getChart();
			source = [=,pos = std::size_t(0),sz = chart.size() ]() mutable {
				if (state.lock_shared()->cancel_upload || pos >= sz) {
					return std::optional<MTrader::ChartItem>();
				}
				state.lock()->upload_progress = (pos * 100)/sz;
				auto ps = rev?sz-pos-1:pos; ++pos;
				return std::optional<MTrader::ChartItem>(chart[ps]);
			};
		} else {
			auto prc = lkst->prices_cache.getSubject();
			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			source = [pos = std::size_t(0), sz = prc.size(), prc = std::move(prc), state , now,rev]() mutable {
				if (state.lock_shared()->cancel_upload || pos >= sz) {
					return std::optional<MTrader::ChartItem>();
				}
				auto ps = rev?sz-pos-1:pos; ++pos;
				double p = prc[ps];
				state.lock()->upload_progress = (pos * 100)/sz;
				std::uint64_t ofs = (sz-pos);
				return std::optional<MTrader::ChartItem>(MTrader::ChartItem{static_cast<uint64_t>(now - ofs*60000),p,p,p});
			};
		}
		if (invert.getBool()) {
			source = [src = std::move(source), fv = std::make_shared<double>(0)]()  {
				std::optional<MTrader::ChartItem> v = src();
				if (v.has_value()) {
					if (*fv == 0) *fv = v->last*v->last;
					return std::optional<MTrader::ChartItem>(MTrader::ChartItem{v->time,*fv/v->bid,*fv/v->ask,*fv/v->last});
				} else {
					return v;
				}
			};
		}

		lkst->cancel_upload = false;
		lkst.release();
		MTrader::VisRes trades = MTrader::visualizeSpread(
				std::move(source),
				sma.getNumber(),
				stdev.getNumber(),
				force_spread.getNumber(),
				mult.getNumber(),
				dynmult_raise.getValueOrDefault(1.0),
				dynmult_fall.getValueOrDefault(1.0),
				dynmult_cap.getValueOrDefault(100.0),
				dynmult_mode.getValueOrDefault("independent"),
				dynmult_sliding.getBool(),
				dynmult_mult.getBool(),
				false,true);

		BacktestCacheSubj bt;
		std::transform(trades.chart.begin(), trades.chart.end(), std::back_inserter(bt.prices), [](const MTrader::VisRes::Item &itm) {
				return BTPrice{itm.time, itm.price};
		});
		bt.minfo = tr->getMarketInfo();
		bt.reversed = rev;
		bt.inverted = invert.getBool();
		lkst = state.lock();
		lkst->upload_progress = -1;
		lkst->backtest_cache = BacktestCache(bt, id.toString().str());
		return true;
	} catch (std::exception &e) {
		logError("Error: $1", e.what());
		state.lock()->upload_progress = -1;
		return false;
	}

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



	s.onIdle(minfo, IStockApi::Ticker{price,price,price,
		static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
				)
	},assets,currency+extra_bal);

	auto range = s.calcSafeRange(minfo, assets, currency);
	auto initial = s.calcInitialPosition(minfo, price, assets, currency+extra_bal);
	Value out = Object
			("min", inverted?1.0/range.max:range.min)
			("max", inverted?1.0/range.min:range.max)
			("initial", (inverted?-1:1)*initial);

	req.sendResponse("application/json", out.toString());
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

	auto wallet = trlist.lock_shared()->walletDB;
	auto lkwallet = wallet.lock_shared();
	json::Value jsn = lkwallet->dumpJSON();
	Array brks;
	trlist.lock_shared()->stockSelector.forEachStock([&](std::string_view brk, const PStockApi &){
		brks.push_back(brk);
	});

	std::unordered_set<json::Value> activeBrokers;

	auto rdc = jsn.reduce([&](std::vector<Value> &&accum, Value row){
		if (accum.empty() || accum.back()["broker"] != row[0] || accum.back()["wallet"] != row[1] || accum.back()["symbol"] != row[2]) {
			Object mdata;
			mdata.set("broker", row[0]);
			mdata.set("wallet", row[1]);
			mdata.set("symbol", row[2]);
			mdata.set("value", row[4]);
			accum.push_back(mdata);
		} else {
			accum.back() = accum.back().replace("value", accum.back()["value"].getNumber()+row[4].getNumber());
		}
		activeBrokers.insert(row[0]);
		return accum;
	},std::vector<Value>());
	jsn = Value(json::array, rdc.begin(), rdc.end(),[](Value x){return x;});

	jsn = Object("entries", Value(json::array, activeBrokers.begin(), activeBrokers.end(),[](Value x){
		return x;
	}))
				("allocations", jsn);
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
	json::StrViewA id = qp["id"];
	json::StrViewA assets = qp["asset"];
	json::StrViewA currency = qp["currency"];
	json::StrViewA sprice = qp["price"];
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
	req.sendResponse("application/json", res.stringify());
	return true;
}
