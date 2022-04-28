/*
 * httpapi.cpp
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#include <imtjson/serializer.h>
#include <imtjson/parser.h>
#include <imtjson/object.h>
#include <imtjson/operations.h>
#include "httpapi.h"

#include <mutex>
#include "../brokers/httpjson.h"
#include "backtest2.h"

#include "istrategy3.h"

#include "trader_factory.h"

#include "abstractExtern.h"

#include "ssestream.h"

using namespace json;
using namespace userver;


bool HttpAPI::get_api__up(Req &req, const Args &args) {
	req->setContentType("text/plain;charset=utf-8");
	req->send("");
	return true;
}

template<typename ACL>
bool HttpAPI::check_acl(Req &req,const ACL &acl) {
	auto auth = core->get_auth().lock();
	auto user = auth->get_user(*req);
	return auth->check_auth(user, acl, *req);

}


bool HttpAPI::get_api_data(Req &req, const Args &args) {
	if (!check_acl(req, ACLSet({ACL::viewer,ACL::config_view}))) return true;
	auto rpt = core->get_report().lock();
	ondra_shared::RefCntPtr<SSEStream> sse = new SSEStream(std::move(req));
	if (!sse->init(true)) return true;
	rpt->addStream([sse](const Report::StreamData &sdata) mutable {
		return sse->on_event(sdata);
	});
	return true;

}

bool HttpAPI::get_api_report_json(Req &req, const Args &args) {
	if (!check_acl(req, ACL::viewer)) return true;
	const MemStorage * rptstorage = core->get_rpt_storage();
	json::Value v = rptstorage->load();
	send_json(req,v);
	return true;
}

bool HttpAPI::get_root(Req &req, const std::string_view &vpath) {
	return static_pages(req, vpath);
}

void HttpAPI::send_json(Req &req, const json::Value &v) {
	req->setContentType(ctx_json);
	Stream stream = req->send();
	v.serialize([&](char c){stream.putCharNB(c);});
	stream.putCharNB('\n');
	stream.flush() >> [req = std::move(req)](Stream &s){
		//closure is necessary to keep pointers alive during async operation
		//destroy stream before the request
		s = Stream();
	};
}

bool HttpAPI::get_api_login(Req &req, const Args &args) {
	HeaderValue auth = req->get("Authorization");
	if (!auth.defined) {
		AuthService::basic_auth(*req);
		return true;
	}
	Auth::User usr = core->get_auth().lock()->get_user(*req);
	if (!usr.exists) {
		AuthService::basic_auth(*req);
		return true;
	}
	HeaderValue redir = args["redir"];
	if (redir.defined) {
		req->setStatus(302);
		req->set("Location", redir);
	}
	req->setContentType("text/plain");
	req->send("Logged in");
	return true;
}

bool HttpAPI::get_api_logout(Req &req, const Args &args) {
	std::string cookie = "auth=;Max-Age=0;Path=";
	auto path = req->getRootPath();
	if (path.empty()) path = "/";
	cookie.append(path);
	req->set("Set-Cookie",cookie);

	HeaderValue auth = req->get("Authorization");
	HeaderValue redir = args[redir];
	if (auth.defined) {
		if (redir.defined) {
			req->set("Refresh", std::to_string(1).append(";").append(redir));
		}
		AuthService::basic_auth(*req);
	} else if (redir.defined) {
		req->set("Location", redir);
		req->setStatus(302);
		req->send("");
	} else {
		send_json(req, true);
	}
	return true;
}

bool HttpAPI::get_api_user(Req &req, const Args &args) {
	auto auth = core->get_auth().lock();
	auto user = auth->get_user(*req);
	json::Object out;
	out.set("user", user.name);
	out.set("exists", user.exists);
	out.set("jwt", user.jwt);
	for (const auto &x: strACL) {
		out.set(x.name, user.acl.is_set(x.val));
	}
	send_json(req, out);
	return true;
}

enum class UserSetCookie {
	temporary,
	permanent,
	retval
};

static json::NamedEnum<UserSetCookie> strUserSetCooke({
	{UserSetCookie::retval, "return"},
	{UserSetCookie::permanent, "permanent"},
	{UserSetCookie::temporary, "temporary"},
});


bool HttpAPI::post_api_user(Req &req, const Args &args) {
	req->readBody(req, max_upload)
			>> [me = PHttpAPI(this)](Req &req, const std::string_view &data) {
		auto auth = me->core->get_auth().lock();
		try {
			json::Value v = json::Value::fromString(data);
			auto juname = v["user"];
			auto jpwd = v["password"];
			auto jtoken = v["token"];
			std::string_view setcookie = v["cookie"].getString();
			std::size_t exp = v["exp"].getValueOrDefault(std::size_t(34560000));
			bool needauth = v["needauth"].getBool();

			UserSetCookie md = strUserSetCooke[setcookie];

			Auth::User user;
			std::string session;

			if (juname.defined()) {
				std::string_view uname = juname.getString();
				std::string_view pwd = jpwd.getString();
				user = auth->get_user(uname, pwd);
			} else if (jtoken.defined()) {
				std::string_view token = jtoken.getString();
				user = auth->get_user(token);
			} else {
				user = auth->get_user(*req);
				if (needauth && !user.exists) {
					AuthService::basic_auth(*req);
					return;
				}
			}
			json::Value retCookie;
			if (user.exists) {
				retCookie = auth->create_session(user, exp);
				if (md != UserSetCookie::retval) {
					std::string cookie = "auth=";
					cookie.append(retCookie.getString());
					cookie.append(";Path=");
					auto root_path = req->getRootPath();
					if (root_path.empty()) root_path = "/";
					cookie.append(root_path);
					if (md == UserSetCookie::permanent) {
						cookie.append(";Max-Age=");
						cookie.append(std::to_string(exp));
					}
					req->set("Set-Cookie", cookie);
					retCookie = json::undefined;
				}

			}
			json::Object out;
			out.set("user", user.name);
			out.set("exists", user.exists);
			out.set("jwt", user.jwt);
			for (const auto &x: strACL) {
				out.set(x.name, user.acl.is_set(x.val));
			}
			out.set("cookie", retCookie);
			me->send_json(req, out);
		} catch (json::UnknownEnumException &e){
			me->api_error(req,400, e.what());
		} catch (json::ParseError &e) {
			me->api_error(req,400, e.what());
		} catch (AdminPartyException &e) {
			me->api_error(req,403, e.what());
		}
	};
	return true;
}

bool HttpAPI::delete_api_user(Req &req, const Args &args) {
	return get_api_logout(req, args);
}

static json::Value brokerToJSON(const std::string_view &id, const IBrokerControl::BrokerInfo &binfo, const std::string_view &root) {
	json::String url;
	if (binfo.exchangeUrl.compare(0,1,"/") == 0) {
		url = json::String({root,"/api/admin/brokers/",HTTPJson::urlEncode(binfo.name),"/page/"});
	}
	else url = binfo.exchangeUrl;

	Value res = Object({
		{"name", binfo.name},
		{"trading_enabled",binfo.trading_enabled},
		{"exchangeName", binfo.exchangeName},
		{"exchangeUrl", url},
		{"version", binfo.version},
		{"subaccounts",binfo.subaccounts},
		{"subaccount",id.substr(std::min(id.length()-1,id.rfind('~'))+1)},
		{"nokeys", binfo.nokeys},
		{"settings",binfo.settings}
	});
	return res;
}


bool HttpAPI::get_api_broker(Req &req, const Args &args) {
	if (!check_acl(req, ACLSet{ACL::config_view,ACL::api_key})) return true;
	Array out;
	core->get_broker_list()->enum_brokers([&](const std::string_view &name, const PStockApi &api){
		IBrokerControl *binfo = dynamic_cast<IBrokerControl *>(api.get());
		if (binfo) {
			out.push_back(brokerToJSON(name, binfo->getBrokerInfo(), req->getRootPath()));
		}
	});
	send_json(req, out);
	return true;
}

bool HttpAPI::get_api_broker_broker(Req &req, const Args &args) {
	if (!check_acl(req, ACLSet{ACL::config_view,ACL::api_key})) return true;
	Array out;
	PStockApi api = core->get_broker_list()->get_broker(args["broker"]);
	IBrokerControl *binfo = dynamic_cast<IBrokerControl *>(api.get());
	if (binfo) {
		out.push_back(brokerToJSON(args["broker"], binfo->getBrokerInfo(), req->getRootPath()));
		send_json(req, out);
	} else {
		api_error(req, 404, "Broker not found");
	}
	return true;
}

bool HttpAPI::delete_api_broker(Req &req, const Args &args) {
	if (!check_acl(req, ACL::config_view)) return true;
	core->get_broker_list()->enum_brokers([&](const std::string_view &name, const PStockApi &api){
		IBrokerInstanceControl *ex = dynamic_cast<IBrokerInstanceControl  *>(api.get());
		if (ex) ex->unload();
	});
	req->setStatus(202);
	send_json(req, true);
	return true;

}

bool HttpAPI::get_api_broker_icon_png(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc) {
		auto binfo = bc->getBrokerInfo();
		req->setContentType("image/png");
		req->send(binfo.favicon);
	} else {
		req->sendErrorPage(404);
	}
	return true;


}

bool HttpAPI::get_api_broker_licence(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc) {
		auto binfo = bc->getBrokerInfo();
		send_json(req, binfo.licence);
	} else {
		api_error(req,404);
	}
	return true;
}

bool HttpAPI::get_api_broker_apikey(Req &req, const Args &args) {
	if (!check_acl(req, ACL::api_key)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto bc = dynamic_cast<IApiKey *>(brk.get());
	if (bc) {
		send_json(req,bc->getApiKeyFields());
	} else {
		api_error(req,404);
	}
	return true;
}

bool HttpAPI::put_api_broker_apikey(Req &req, const Args &args) {
	req->readBody(req, max_upload) >> [me=PHttpAPI(this), broker = std::string(args["broker"])]
				(Req &req, const std::string_view &body) {
		if (!me->check_acl(req, ACL::api_key)) return;
		try {
			json::Value data = json::Value::fromString(body);
			auto brk = me->core->get_broker_list()->get_broker(broker);
			auto bc = dynamic_cast<IApiKey *>(brk.get());
			if (bc) {
				try {
					bc->setApiKey(data);
				} catch (const AbstractExtern::Exception &e) {
					if (e.isResponse()) {
						me->api_error(req, 409, e.what());
						return;
					} else {
						throw;
					}
				}
				me->send_json(req,true);
			} else {
				me->api_error(req,404);
			}
		} catch (ParseError &e) {
			me->api_error(req,400, e.what());
		}
	};
	return true;

}

bool HttpAPI::delete_api_broker_apikey(Req &req, const Args &args) {
	auto broker = std::string(args["broker"]);
	if (!check_acl(req, ACL::api_key)) return true;
	auto brk = core->get_broker_list()->get_broker(broker);
	auto bc = dynamic_cast<IApiKey *>(brk.get());
	if (bc) {
		try {
			bc->setApiKey(nullptr);
		} catch (const AbstractExtern::Exception &e) {
			if (e.isResponse()) {
				api_error(req, 409, e.what());
				return true;
			} else {
				throw;
			}
		}
		send_json(req,true);
	} else {
		api_error(req,404);
	}
	return true;
}


bool HttpAPI::get_api_broker_wallet(Req &req, const Args &args) {
	auto broker = std::string(args["broker"]);
	if (!check_acl(req, ACL::wallet_view)) return true;
	auto brk = core->get_broker_list()->get_broker(broker);
	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc) {
		try {
			Object wallet;
			IBrokerControl::AllWallets allw = bc->getWallet();
			for (auto &&x: allw) {
				Object bww;
				for (auto &&y: x.wallet) {
					bww.set(y.symbol, y.balance);
				}
				wallet.set(x.walletId, bww);
			}
			send_json(req, wallet);
			return true;
		} catch (std::exception &e) {
			api_error(req, 409, e.what());
		}
	} else {
		api_error(req, 404);
	}
	return true;

}


bool HttpAPI::get_api_broker_pairs(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto flat = args["flat"] == "true";

	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc) {
		json::Value v = bc->getMarkets();
		if (flat) {
			Array out;
			v.walk([&](json::Value x){
				if (x.type() == json::string) out.push_back(x.stripKey());
				return true;
			});
			v = out;
		}
		send_json(req,v);
	} else {
		api_error(req,404);
	}
	return true;

}


bool HttpAPI::get_api_broker_pairs_pair(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string_view pair = args["pair"];
	if (brk != nullptr) {
		try {
			IStockApi::MarketInfo minfo = brk->getMarketInfo(pair);
			send_json(req, minfo.toJSON());
		} catch (AbstractExtern::Exception &e) {
			api_error(req,404, e.what());
		}
	} else {
		api_error(req,404, "Broker not found");
	}
	return true;
}

static json::Value tickerToJSON(const IStockApi::Ticker &tk) {
	return Object {
		{"bid",tk.bid},
		{"ask", tk.ask},
		{"last", tk.last},
		{"time", tk.time}
	};
}


bool HttpAPI::get_api_broker_pairs_ticker(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string_view pair = args["pair"];
	if (brk != nullptr) {
		std::unique_lock lk(core->get_cycle_lock(),std::try_to_lock);
		try {
			if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
			IStockApi::Ticker tk = brk->getTicker(pair);
			send_json(req, tickerToJSON(tk));
		} catch (AbstractExtern::Exception &e) {
			api_error(req,404, e.what());
		}
	} else {
		api_error(req,404, "Broker not found");
	}
	return true;
}

void HttpAPI::api_error(Req &req, int err, std::string_view desc) {
	if (desc.empty()) {
		desc = getStatusCodeMsg(err);
	}
	req->setStatus(err);
	send_json(req, Object {{"error",Object{
		{"code", err},
		{"message",desc }}}
	});
}


bool HttpAPI::post_set_cookie(Req &req, const Args &args) {
	req->readBody(req,max_upload)
		>> [me = PHttpAPI(this)](Req &req, const std::string_view &body) {
			QueryParser qp;
			qp.parse(body, true);
			HeaderValue auth = qp["auth"];
			if (auth.defined) {
				std::string cookie = "auth=";
				cookie.append(auth);
				req->set("Set-Cookie", cookie);
			} else {
				req->sendErrorPage(400);
				return;
			}
			HeaderValue redir = qp["redir"];
			if (redir.defined && !redir.empty()) {
				req->set("Location", redir);
				req->setStatus(302);
			} else {
				req->setStatus(202);
			}
			req->send("");
		};
	return true;
}

bool HttpAPI::get_api_broker_pairs_settings(Req &req, const Args &args) {
	if (!check_acl(req, ACL::config_view)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string_view pair = args["pair"];
	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc != nullptr) {
		try {
			send_json(req, bc->getSettings(pair));
		} catch (AbstractExtern::Exception &e) {
			api_error(req,404, e.what());
		}
	} else {
		api_error(req,404);
	}
	return true;
}

bool HttpAPI::put_api_broker_pairs_settings(Req &req, const Args &args) {
	if (!check_acl(req, ACL::config_edit)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
    auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc != nullptr) {
		req->readBody(req,max_upload) >> [bc,brk,me=PHttpAPI(this)](Req &req, std::string_view body) {
			(void)brk;
			try {
				Value v = Value::fromString(body);
				Value all_settings = bc->setSettings(v);
				me->send_json(req, all_settings);
			} catch (AbstractExtern::Exception &e) {
				me->api_error(req,409, e.what());
			} catch (ParseError &e) {
				me->api_error(req,400, e.what());
			}
		};
	} else {
		api_error(req,404);
	}
	return true;
}




json::Value HttpAPI::merge_JSON(json::Value src, json::Value diff) {
	if (diff.type() == json::object) {
		if (diff.empty()) return json::undefined;
		if (src.type() != json::object) src = json::object;
		auto src_iter = src.begin(), src_end = src.end();
		auto diff_iter = diff.begin(), diff_end = diff.end();
		json::Object out;
		while (src_iter != src_end && diff_iter != diff_end) {
			auto src_v = *src_iter, diff_v = *diff_iter;
			auto src_k = src_v.getKey(), diff_k = diff_v.getKey();
			if (src_k < diff_k) {
				out.set(src_v);
				++src_iter;
			} else if (src_k > diff_k) {
				out.set(diff_k, merge_JSON(json::undefined, diff_v));
				++diff_iter;
			} else {
				out.set(diff_k, merge_JSON(src_v, diff_v));
				++src_iter;
				++diff_iter;
			}
		}
		while (src_iter != src_end) {
			out.set(*src_iter);
			++src_iter;
		}
		while (diff_iter != diff_end) {
			auto diff_v = *diff_iter;
			out.set(diff_v.getKey(), merge_JSON(json::undefined, diff_v));
			++diff_iter;
		}
		return out;
	} else if (diff.type() == json::undefined){
		return src;
	} else {
		return diff;
	}
}

json::Value HttpAPI::make_JSON_diff(json::Value src, json::Value trg) {
	if (trg.type() != json::object) {
		if (trg.defined()) {
			if (trg != src)	return trg;
			else return json::undefined;
		}
		else if (src.defined()) return json::object;
		else return json::undefined;
	}
	if (src.type() != json::object) {
		if (trg.type() == json::object && trg.empty()) {
			return json::Value(json::object, {json::Value("",json::object)});
		} else {
			src = json::object;
		}
	}
	auto src_iter = src.begin(), src_end = src.end();
	auto trg_iter = trg.begin(), trg_end = trg.end();
	json::Object out;
	while (src_iter != src_end && trg_iter != trg_end) {
		auto src_v = *src_iter, trg_v = *trg_iter;
		auto src_k = src_v.getKey(), trg_k = trg_v.getKey();
		if (src_k < trg_k) {
			out.set(src_k, make_JSON_diff(src_v, json::undefined));
			++src_iter;
		} else if (src_k > trg_k) {
			out.set(trg_k, make_JSON_diff(json::undefined, trg_v));
			++trg_iter;
		} else {
			out.set(trg_k, make_JSON_diff(src_v, trg_v));
			++src_iter;
			++trg_iter;
		}
	}
	while (src_iter != src_end) {
		auto src_v = *src_iter;
		auto src_k = src_v.getKey();
		out.set(src_k, make_JSON_diff(src_v, json::undefined));
		++src_iter;
	}
	while (trg_iter != trg_end) {
		auto trg_v = *trg_iter;
		auto trg_k = trg_v.getKey();
		out.set(trg_k, make_JSON_diff(json::undefined, trg_v));
		++trg_iter;
	}
	json::Value o = out;
	if (o.empty()) return json::undefined; else return o;

}

bool HttpAPI::get_api_config(Req &req, const Args &) {
	auto auth = core->get_auth().lock();
	auto user = auth->get_user(*req);
	if (!auth->check_auth(user, ACL::config_view, *req)) return true;
	auto cfg = core->get_config();
	if (!user.acl.is_set(ACL::users)) cfg.setItems({
		{"users",json::undefined},
		{"session_hash",json::undefined}
	});
	send_json(req, cfg);
	return true;
}


bool HttpAPI::post_api_config(Req &req, const Args &v) {
	req->readBody(req, max_upload) >> [me = PHttpAPI(this)](Req &req, std::string_view text) {
		auto auth = me->core->get_auth().lock();
		auto user = auth->get_user(*req);
		if (!auth->check_auth(user, ACL::config_edit, *req)) return;
		try {
			auth.release();
			std::lock_guard _(me->cfg_lock);
			Value cfg = me->core->get_config();
			Value cfg_merged;
			userver::trim(text);
			if (text.empty()) {
				cfg_merged = cfg;
			} else {
				Value cfg_diff = Value::fromString(text);
				if (!user.acl.is_set(ACL::users)) cfg_diff.setItems({
					{"users",json::undefined},
					{"session_hash",json::undefined}
				});
				cfg_merged = merge_JSON(cfg, cfg_diff);
				if (!cfg_merged.defined()) cfg_merged = cfg;
				Value users = cfg_merged["users"];
				if (users.defined()) {
					users = AuthService::conv_pwd_to_hash(users);
					cfg_merged.setItems({{"users",users}});
				}
			}
			me->core->setConfig(cfg_merged);
			req->setStatus(202);
			me->send_json(req, cfg_merged);
		} catch (const ParseError &e) {
			me->api_error(req, 400, e.what());
		}

	};
	return true;
}

enum class FormType {
	strategies, spread_gens, trader
};

static NamedEnum<FormType> strFormType({
	{FormType::strategies,"strategies"},{FormType::spread_gens,"spread_gens"},{FormType::trader,"trader"}
});

json::Value tooldesc_to_JSON(const ToolDescription &desc) {
	return Value(json::object, {
			Value("name", desc.name),
			Value("category", desc.category),
			Value("form_def", desc.form_def),
			Value("id", desc.id)
	});
}

bool HttpAPI::get_api_form(Req &req, const Args &v) {
	Object forms;

	auto append_tool = [&](std::string_view id, const auto &tool) {
		forms.set(id, Value(json::array, tool.begin(), tool.end(), [](const auto &x){
			return tooldesc_to_JSON(x.second->get_description());
		}));
	};

	append_tool("strategies", StrategyRegister::getInstance());
	append_tool("spread_generators", SpreadRegister::getInstance());
	forms.set("trader", get_trader_form());

	json::Value acls(array,strACL.sorted_by_value().begin(), strACL.sorted_by_value().end(), [](const NamedEnum<ACL>::Def &d) {
		return d.name;
	});
	forms.set("acls", acls);

	send_json(req, forms);
	return true;

}

bool HttpAPI::get_api_user_set_password(Req &req, const Args &v) {
	req->readBody(req, max_upload) >> [me = PHttpAPI(this)](Req &req, const std::string_view &text) {
		std::lock_guard _(me->cfg_lock);
		auto auth = me->core->get_auth().lock();
		Auth::User user = auth->get_user(*req);
		if (!user.exists) {
			me->api_error(req, 401, "Not logged");
		} else {
			try {
				json::Value p = json::Value::fromString(text);
				std::string_view old_pwd = p["old"].getString();
				std::string_view new_pwd = p["new"].getString();
				if (new_pwd.empty()) {
					me->api_error(req, 400, "Password can't be empty");
				} else if (new_pwd == old_pwd) {
					me->api_error(req, 400, "No change made");
				} else {
					Value cfg = me->core->get_config();
					bool b = auth->change_password(cfg, user, old_pwd, new_pwd);
					if (!b) {
						me->api_error(req, 409, "Old password doesn't match");
					} else {
						me->core->storeConfig(cfg);
						req->setStatus(202);
						me->send_json(req, true);
					}
				}
			} catch (const ParseError &e) {
				me->api_error(req, 400, e.what());
			}
		}
	};
	return true;

}

bool HttpAPI::post_api_backtest_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	req->readBody(req, max_upload) >> [me = PHttpAPI(this)](Req &req, const std::string_view &text) {
		try {
			json::Value data = json::Value::fromString(text);
			if (data.type() == json::array && data.size()>0) {
				auto x = data.find([](const json::Value &z){
					return z.type() != json::number;
				});
				if (!x.defined()) {
					auto bk = me->core->get_backtest_storage().lock();
					std::string id = bk->store_data(data);
					req->set("Location",std::string(req->getRootPath()).append("/api/backtest/data/").append(id));
					req->setStatus(201);
					me->send_json(req, Object{{"id",id}});
					return;
				}
			}
			me->api_error(req, 400, "Expected array of numbers");
		} catch (const ParseError &e) {
			me->api_error(req, 400, e.what());
		}
	};
	return true;
}

bool HttpAPI::get_api_backtest_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	auto bk = core->get_backtest_storage().lock();
	json::Value z = bk->load_data(std::string(v["id"]));
	if (z.defined()) {
		send_json(req, z);
	} else {
		api_error(req, 404, "Not found. Please reupload");
	}
	return true;



}

bool HttpAPI::post_api_backtest_historical_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	if (core->get_backtest_broker() == nullptr) {
		api_error(req,503, "Service is not configured");
		return true;
	}
	req->readBody(req, max_upload) >> [me = PHttpAPI(this)](Req &req, const std::string_view &text) {
		try {
			json::Value data = json::Value::fromString(text);
			unsigned int smooth = data["smooth"].getUInt();
			auto from = std::chrono::system_clock::to_time_t(
					std::chrono::system_clock::now()-std::chrono::hours(365*24)
			);
			from = (from/86400)*86400;
			json::Value chart_data = me->core->get_backtest_broker()->get_minute(
					data["asset"].getString(),
					data["currency"].getString(),
					from
			);
			if (smooth>1) {
				double accum = chart_data[0].getNumber()*smooth;
				Value smth_data (json::array,chart_data.begin(), chart_data.end(),[&](const Value &d){
					accum -= accum / smooth;
					accum += d.getNumber();
					return accum/smooth;
				});
				chart_data = smth_data;
			}
			auto bk = me->core->get_backtest_storage().lock();
			std::string id = bk->store_data(chart_data);
			req->set("Location",std::string("data/").append(id));
			req->setStatus(201);
			me->send_json(req, Object{{"id",id}});
			return;
		} catch (const ParseError &e) {
			me->api_error(req, 400, e.what());
		}
	};
	return true;
}

bool HttpAPI::get_api_backtest_historical_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	if (core->get_backtest_broker() == nullptr) {
		api_error(req,503, "Service is not configured");
		return true;
	}
	send_json(req,core->get_backtest_broker()->get_symbols());
	return true;

}

bool HttpAPI::get_api_backtest_historical_data_broker_pair(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_view)) return true;
	auto brk = core->get_broker_list()->get_broker(v["broker"]);
	std::string_view pair = v["pair"];
	if (brk != nullptr) {
		try {
			auto minfo = brk->getMarketInfo(pair);
			auto bc = dynamic_cast<IHistoryDataSource *>(brk.get());
			bool res = bc && bc->areMinuteDataAvailable(minfo.asset_symbol, minfo.currency_symbol);
			if (res) send_json(req, true);
			else api_error(req, 404, "Not available");
		} catch (std::exception &e) {
			api_error(req, 404, e.what());
		}
	} else {
		api_error(req,404, "Broker not found");
	}
	return true;
}

class HttpAPI::DataDownloaderTask {
public:
	int id;
	PSSEStream sse;
	PCancelMap cancel_map;
	std::string pair;
	PBacktestStorage storage;
	PStockApi api;
	IHistoryDataSource *hds;
	IStockApi::MarketInfo minfo;
	AsyncProvider async;

	std::vector<double> tmpVect;
	std::stack<std::vector<double> > datastack;
	std::uint64_t end_tm ;
	std::uint64_t start_tm;
	std::size_t cnt;
	std::uint64_t n;

	void init();
	void operator()();
	void done();
};

void HttpAPI::DataDownloaderTask::init() {
	auto now = std::chrono::system_clock::now();
	end_tm = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	start_tm = end_tm-24ULL*60ULL*60ULL*1000ULL*365ULL;
	cnt = 0;
	n = end_tm;
	cancel_map.lock()->lock(id);
}


void HttpAPI::DataDownloaderTask::operator()() {
	auto bc = dynamic_cast<IHistoryDataSource *>(api.get());
	auto progress = n?((end_tm - n)/((double)(end_tm-start_tm)/100)):100;
	bool cont = sse->on_event({true,Object{{"set_progress",progress}}});
	bool canceled = cancel_map.lock_shared()->is_canceled(id);
	if (bc && n && cont && !canceled && n > start_tm) {
		try {
			n = bc->downloadMinuteData(
					minfo.asset_symbol,
					minfo.currency_symbol,
					pair, start_tm, n, tmpVect);
		} catch (std::exception &e) {
			bool cont = sse->on_event({true,Object {{"set_error", e.what()}}});
			if (cont) {
				std::this_thread::sleep_for(std::chrono::seconds(2));
				async.runAsync(std::move(*this));
			} else {
				cancel_map.lock()->unlock(id);
			}
			return;
		}
		cnt+=tmpVect.size();
		datastack.push(std::move(tmpVect));
		tmpVect.clear();
		async.runAsync(std::move(*this));
	} else {
		if (cont) done();
		cancel_map.lock()->unlock(id);
		sse->close();

	}

}
void HttpAPI::DataDownloaderTask::done() {
	tmpVect.reserve(cnt);
	while (!datastack.empty()) {
		const auto &p = datastack.top();
		tmpVect.insert(tmpVect.end(), p.begin(), p.end());
		datastack.pop();
	}
	Value out = json::array;
	if (!tmpVect.empty()) {
		out = Value(json::array, tmpVect.begin(), tmpVect.end(),[&](const auto &x) -> Value {
			return x;
		});
	}
	std::string ret = storage.lock()->store_data(out);
	sse->on_event({true,Object{{"set_state","done"},{"id", ret}}});
}



bool HttpAPI::post_api_backtest_historical_data_broker_pair(Req &req,const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	try {
		auto brk = core->get_broker_list()->get_broker(v["broker"]);
		auto bc = dynamic_cast<IHistoryDataSource *>(brk.get());
		std::string pair(v["pair"]);
		auto minfo = brk->getMarketInfo(pair);
		auto storage = core->get_backtest_storage();
		if (bc != nullptr) {
			int ret = reg_op_map.lock()->push([=](PSSEStream sse, int id){
				auto async = userver::getCurrentAsyncProvider();
				DataDownloaderTask task {id, sse, cancel_map, pair,storage,brk,bc,minfo,async};
				task.init();
				async.runAsync(std::move(task));
			});
			redir_to_run(ret, req);
		} else {
			api_error(req,404, "Broker not found");
		}
	} catch (std::exception &e) {
		api_error(req, 404, e.what());
	}


	return true;

}

bool HttpAPI::get_api_run(Req &req, const Args &v) {
	int id = v["id"].getUInt();
	auto mp = reg_op_map.lock();
	if (mp->check(id)) {
		PSSEStream s = new SSEStream(std::move(req));
		if (s->init(true)) {
			auto cb = mp->pop(id);
			if (cb != nullptr) {
				cb(s,id);
			}
		}
	} else {
		api_error(req, 404, "No active operation");
	}
	return true;
}

bool HttpAPI::delete_api_run(Req &req, const Args &v) {
	int id = v["id"].getUInt();
	if (cancel_map.lock()->set_canceled(id)) {
		send_json(req, true);
	} else {
		auto mp = reg_op_map.lock();
		if (mp->check(id)) {
			mp->pop(id);
			send_json(req, true);
		} else {
			api_error(req, 404, "No operation");
		}
	}
	return true;

}

class HttpAPI::BacktestState {
public:

	using Done = userver::Callback<void()>;
	using CheckCancel = userver::Callback<bool()>;

	PSSEStream sse;
	std::unique_ptr<Backtest> bt;
	bool trades_only;
	bool output_strategy_state;
	bool output_strategy_raw_state;
	bool output_spread_raw_state;
	bool output_orders;
	bool output_stats;
	bool output_debug;
	bool output_log;
	bool output_diff;
	json::Value prev_output;
	int trade_counter = 0;

	std::size_t prev_err_hash = 0;

	static void run(std::unique_ptr<BacktestState> &&me);
	Done done;
	CheckCancel check_cancel;
	std::chrono::system_clock::time_point next_progress_report;



};


void HttpAPI::redir_to_run(int id, Req &req) {
	req->setStatus(201);
	std::string loc(req->getRootPath());
	loc.append("/api/run/");
	loc.append(std::to_string(id));
	req->set("Location", loc);
	send_json(req, Object { { "id", id }, { "link", loc } });
}

bool HttpAPI::post_api_backtest(Req &req, const Args &v) {
	bool need_stream = v["stream"] == "true";
	req->readBody(req, max_upload) >> [me=PHttpAPI(this), need_stream](Req &req, const std::string_view &text) {
		if (!me->check_acl(req, ACL::backtest)) return;
		try {
			Value r = Value::fromString(text);
			auto source = r["source"];
			auto output = r["output"];
			auto init = r["init"];
			std::string source_id (source["id"].getString());
			json::Value jdata = source["data"];
			json::Value price_data;
			if (!jdata.empty()) {
				price_data = jdata;
			} else {
				auto bstr = me->core->get_backtest_storage().lock();
				price_data = bstr->load_data(source_id);
			}

			if (price_data.type() != json::array) {
				me->api_error(req, 404, "Source data are not available");return;
			}


			auto offset = source["offset"].getUInt();
			if (offset >= price_data.size()) {
				me->api_error(req, 416, "Offset out of range");return;
			}
			auto limit = source["limit"].getUInt();

			std::vector<float> data; //prices are stored as float - to save some memory
			auto count = std::min<UInt>(price_data.size()-offset,limit?limit:price_data.size());
			data.reserve(count);
			for (UInt i = 0; i < count; i++) {
				double v = price_data[i+offset].getNumber();
				if (v <= 0.0) {
					me->api_error(req, 409, "Unusable data, contains zero, NaN or negative price");
				}
				data.push_back(v);
			}

			if (source["backward"].getBool()) {
				for (UInt mx = count/2, i = 0;i<mx;i++) {
					std::swap(data[i], data[count-i]);
				}
			}
			if (source["invert"].getBool()) {
				double fval = data[0];
				std::transform(data.begin(), data.end(), data.begin(), [&](double x){
					return fval*fval/x;
				});
			}
			bool mirror = source["mirror"].getBool();

			{
				double init_price = source["init_price"].getNumber();
				if (init_price) {
					double m = init_price/data[0];
					std::transform(data.begin(), data.end(), data.begin(), [&](double x){
						return x*m;
					});
				}
			}


			Trader_Config_Ex tcfg;
			tcfg.parse(r["trader"]);
			auto strategy = StrategyRegister::getInstance().create(tcfg.strategy_id, tcfg.strategy_config);
			auto spread = SpreadRegister::getInstance().create(tcfg.spread_id, tcfg.spread_config);
			auto minfo = IStockApi::MarketInfo::fromJSON(r["market"]);


			double init_balance = init["balance"].getNumber();
			auto init_position_j = init["position"];
			double init_position;
			if (init_position_j.type() == json::number) {
				init_position = init_position_j.getNumber();
			} else {
				MarketState st = {};
				st.minfo = &minfo;
				st.balance = st.equity = st.event_equity = init_balance;
				st.cur_price = st.open_price = st.event_price = data[0];

				init_position = strategy->calc_initial_position(st);
				if (minfo.leverage <= 0) {
					init_balance -= init_position * data[0];
				}
			}
			if (init_balance <= 0 && init_position == 0) {
				me->api_error(req, 409, "No initial balance");return;
			}

			if (mirror) {
				UInt p = data.size();
				data.reserve(p*2);
				while (p) {
					p--;
					data.push_back(data[p]);
				}
			}

			auto st = std::make_unique<BacktestState>();

			st->bt = std::make_unique<Backtest>(tcfg,minfo,init_position, init_balance);

			std::uint64_t start_time = source["start_time"].getUIntLong();
			if (start_time == 0) start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();

			st->bt->start(std::move(data), start_time);
			st->trades_only = output["freq"].getString() != "minute";
			st->output_strategy_state = output["strategy_state"].getBool();
			st->output_strategy_raw_state = output["strategy_raw_state"].getBool();
			st->output_spread_raw_state = output["strategy_raw_state"].getBool();
			st->output_orders = output["orders"].getBool();
			st->output_diff = output["diff"].getBool();
			st->output_stats = output["stats"].getBool();
			st->output_debug = output["debug"].getBool();
			st->output_log = output["log"].getBool();

			if (need_stream) {

				HttpAPI *thisptr = me; //we don't need refcounted ptr, this would create cycle
				int id = me->reg_op_map.lock()->push([thisptr,st = std::move(st)](PSSEStream sse, int id) mutable {
					thisptr->cancel_map.lock()->lock(id);
					auto cancel_map = thisptr->cancel_map;
					st->sse = sse;
					st->check_cancel = [cancel_map, id]() mutable {
						return cancel_map.lock_shared()->is_canceled(id);
					};
					st->done = [cancel_map, id]() mutable {
						cancel_map.lock()->unlock(id);
					};
					st->run(std::move(st));
				});
				redir_to_run(id, req);
			} else{
				st->sse = new SSEStream(std::move(req));
				st->sse->init(false);
				st->check_cancel = []() {return false;};
				st->done = [](){};
				st->run(std::move(st));
			}
		} catch (const ParseError &e) {
			me->api_error(req,400, e.what());
		}
	};
	return true;


}

void HttpAPI::BacktestState::run(std::unique_ptr<BacktestState> &&ptr) {
	BacktestState *me = ptr.get();
	static std::hash<std::string> hstr;
	static auto no_error = 3*hstr("");
	while (!me->check_cancel() && me->bt->next()) {

		auto now = std::chrono::system_clock::now();
		if (now > me->next_progress_report) {
			if (!me->sse->on_event({true,Object{
				{"event","progress"},
				{"progress", me->bt->get_progress()}
			}})) {
				break;
			}
			me->next_progress_report = now + std::chrono::seconds(1);
		}

		const auto &trades = me->bt->get_trades();
		int trade_count = trades.length - me->trade_counter;
		me->trade_counter =trades.length;
		const auto lg = me->bt->get_log_msgs();
		const std::string buy_error = me->bt->getBuyErr();
		const std::string sell_error = me->bt->getSellErr();
		const std::string gen_error = me->bt->getGenErr();
		std::size_t errh = hstr(buy_error)+hstr(sell_error)+hstr(gen_error);
		bool err_event = errh != me->prev_err_hash && errh != no_error;
		bool err_clear_event = errh != me->prev_err_hash && errh == no_error;

		me->prev_err_hash = errh;
		bool log_event = (me->output_log && !lg.empty());
		if (!me->trades_only || trade_count || log_event || err_event || err_clear_event){


			Object output;
			output.setItems({
				{"time",me->bt->get_cur_time()},
				{"price",me->bt->get_cur_price()},
				{"event",trade_count?"trade":err_event?"error":log_event?"log_msg":err_clear_event?"error_clear":"minute"}
			});
			if (trade_count) {
				auto jtrl = output.array("trades");
				for (int t = 0; t < trade_count; t++) {
					const IStatSvc::TradeRecord &tr = trades[trades.length-trade_count+t];
					jtrl.push_back(tr.toJSON());
				}
			}
			if (me->output_orders) {
				auto ord = output.object("orders");
				std::optional<IStockApi::Order> buy_order = me->bt->get_buy_order();
				std::optional<IStockApi::Order> sell_order = me->bt->get_buy_order();
				if (buy_order.has_value()) ord.set("buy",buy_order->toJSON());
				if (sell_order.has_value()) ord.set("sell",sell_order->toJSON());
			}
			if (me->output_stats) {
				auto stats = output.object("stats");
				const auto &misc = me->bt->get_misc_data();
				stats.setItems({
					{"position", me->bt->get_sim_position()},
					{"balance", me->bt->get_sim_balance()},
					{"equity",me->bt->get_sim_equity()},
					{"alloc_equity",me->bt->get_trader().get_equity_allocation()},
					{"equilibrium",misc.equilibrium},
					{"highest_price",misc.highest_price},
					{"entry_price",misc.entry_price},
					{"lowest_price",misc.lowest_price},
					{"budget_extra",misc.budget_extra},
					{"rpnl",misc.rpnl},
					{"upnl",misc.upnl},
					{"neutral_price", me->bt->get_trader().get_neutral_price()},
				});
			}
			if (me->output_strategy_state) {
				output.set("strategy", me->bt->get_trader().get_strategy_report());
			}
			if (me->output_strategy_raw_state) {
				output.set("strategy_raw", me->bt->get_trader().get_strategy().save());
			}
			if (me->output_spread_raw_state) {
				output.set("spread_raw", me->bt->get_trader().get_spread().save());
			}
			{
				auto err = output.object("errors");
				if (!buy_error.empty()) err.set("buy",buy_error);
				if (!sell_error.empty()) err.set("sell",sell_error);
				if (!gen_error.empty()) err.set("generic",gen_error);
			}
			if (me->output_debug) {
				const auto &misc = me->bt->get_misc_data();
				auto debug = output.object("debug");
				debug.setItems({
					{"accumulated",misc.accumulated},
					{"achieve_mode",misc.achieve_mode},
					{"budget_assets",misc.budget_assets},
					{"budget_total",misc.budget_total},
					{"dynmult_buy",misc.dynmult_buy},
					{"dynmult_sell",misc.dynmult_sell},
					{"lastTradePrice",misc.lastTradePrice},
					{"position",misc.position},
					{"spread",misc.spread},
					{"trade_dir",misc.trade_dir},
					{"report_position",me->bt->get_position()},
				});
			}

			if (me->output_log && lg.empty()) {
				output.set("log",Value(array, lg.begin(), lg.end(), [&](const Backtest::LogMsg &msg){
					return msg.text;
				}));
			};

			json::Value out_raw = output;
			json::Value out;
			if (me->output_diff) {
				out = make_JSON_diff(me->prev_output, out_raw);
				me->prev_output = out_raw;
			} else {
				out = out_raw;
			}

			if (!me->sse->on_event({true, out})) {
				break;
			}

			if (me->sse->wait_if_buffer_full(100000, [&]{
				return [me = std::move(ptr)]() mutable {
					me->run(std::move(me));
				};
			})) {
				return;
			}
		}

	}
	me->sse->on_event({true, Object{{"event","done"}}});
	me->sse->close();
	me->done();
}

bool HttpAPI::post_api_editor(Req &req, const Args &v)  {
	req->readBody(req, max_upload)>> [me=PHttpAPI(this)](Req &req, const std::string_view &buffer) mutable {
			try {
				if (!me->check_acl(req, ACL::config_view)) return;

				Value data = Value::fromString(buffer);
				Value rbroker = data["broker"];
				Value rtrader = data["trader"];
				Value rsymb = data["pair"];
				Value rswap = data["swap_mode"];

				me->trader_info(req, rtrader.getString(), rbroker.getString(), rsymb.getString(), rswap.getUInt(), true);
			} catch (ParseError &e) {
				api_error(req, 400, e.what());
			}
	};
	return true;
}


void HttpAPI::trader_info(Req &req, std::string_view trader_id, std::string_view broker_id, std::string_view pair_id, unsigned int swap_id, bool vis) {
	std::size_t uid;
	bool exists = false;
	bool stopped = false;

	auto traders = core->get_traders().lock_shared();

	PTrader tr = traders->get_trader(trader_id);


	PStockApi api;
	MarketState mst = {};
	IStockApi::MarketInfo minfo;
	Value strategy_state;
	Value strategy_raw_state;
	Value spread_raw_state;
	Value visstrategy;
	MinMax safe_range = {};
	IStockApi::Orders open_orders;
	Trader_Config cfg;
	std::size_t partial_executions = 0;
	ACB partial_offset(0.0, 0.0);
	double live_assets = 0;
	double live_currency = 0;
	double neutral_price = 0;
	double eq_alloc = 0;
	double eq = 0;
	double target_buy = 0, target_sell =0;
	std::optional<Trader::AchieveMode> achieve;
	if (tr == nullptr) {
		auto extBal = core->get_ext_balance().lock_shared();
		std::unique_lock  lk(core->get_cycle_lock(),std::try_to_lock);
		try {
			api = core->get_broker_list()->get_broker(broker_id);
			api = selectStock(api, static_cast<SwapMode3>(swap_id), false);
			if (api == nullptr) {
				api_error(req,409,"Broker not found");
				return;
			}
			if (lk.owns_lock()) api->reset(std::chrono::system_clock::now());
			minfo = api->getMarketInfo(pair_id);
			IStockApi::Ticker ticker = api->getTicker(pair_id);
			mst.cur_time = mst.event_time = ticker.time;
			mst.cur_price = mst.event_price = ticker.last;
			live_assets = api->getBalance(minfo.asset_symbol, pair_id);
			live_currency = api->getBalance(minfo.currency_symbol, pair_id);
			mst.event = MarketEvent::idle;
			mst.live_assets = live_assets+extBal->get(broker_id, minfo.wallet_id, minfo.asset_symbol);
			mst.live_currencies = live_currency+extBal->get(broker_id, minfo.wallet_id, minfo.currency_symbol);
			mst.sug_sell_price=mst.lowest_sell_price = ticker.ask;
			mst.sug_buy_price=mst.highest_buy_price = ticker.bid;
			uid = 0;
			exists=false;
		} catch (std::exception &e) {
			api_error(req, 409, e.what());
			return;
		}
	} else {
		auto trl = tr.lock_shared();
		auto balCache = core->get_balance_cache().lock_shared();
		auto walletDB = traders->get_wallet_db().lock_shared();
		cfg = trl->get_config();
		pair_id = cfg.pairsymb;
		broker_id = cfg.broker;
		api = trl->get_exchange();
		mst = trl->get_market_state();
		stopped = trl->is_stopped();
		minfo = *mst.minfo;
		uid = trl->get_UUID();
		partial_executions = trl->get_incomplete_trade_count();
		partial_offset = trl->get_position_offset();
		target_buy = trl->get_target_buy();
		target_sell = trl->get_target_sell();
		eq_alloc = trl->get_equity_allocation();
		eq = trl->get_equilibrium();
		auto stratobj = trl->get_strategy();
		strategy_state = trl->get_strategy_report();
		strategy_raw_state = stratobj.save();
		spread_raw_state = trl->get_spread().save();
		exists = true;
		neutral_price = trl->get_neutral_price();
		live_assets = balCache->get(broker_id, minfo.wallet_id, minfo.asset_symbol);
		live_currency = balCache->get(broker_id, minfo.wallet_id, minfo.currency_symbol);

		safe_range = trl->get_safe_range();

		if (vis) {
			double g_beg = std::min<double>(std::max<double>(0,safe_range.min),mst.cur_price*0.9);
			double g_end = std::max<double>(std::min<double>(safe_range.max, 4*mst.cur_price - g_beg),mst.cur_price*1.1);

			struct Pt {
				double x,b,h,p,y;
			};
			std::vector<Pt> points;
			double prev_y = 0;
			for (int i = 0; i < 200; i++) {
				double x = g_beg+(g_end-g_beg)*(i/200.0);
				auto pt = stratobj.get_chart_point(x);
				auto pt2 = stratobj.get_chart_point(x*1.01);
				if (pt.valid && std::isfinite(pt.equity) && std::isfinite(pt.position)) {
					double y = pt2.valid?pt.position*x*0.01+pt.equity-pt2.equity:0;
					if (y < 0) y = prev_y;
					else prev_y = y;
					points.push_back({
						minfo.invert_price?1.0/x:x,
							pt.equity,
							std::abs(pt.position*x),
							pt.position,y});
				}
			}

			while (!points.empty() && std::abs(points.back().p) < minfo.asset_step) {
				points.pop_back();
			}
			auto cp = stratobj.get_chart_point(mst.cur_price);

			json::Array tangent;
			for (int i = -50; i <50;i++) {
				double x = mst.cur_price+(g_end-g_beg)*(i/200.0);
				tangent.push_back({
					minfo.invert_price?1.0/x:x,
					cp.position*(x-mst.cur_price)+cp.equity
				});
			}




			visstrategy = json::Object{
				{"points",json::Value(json::array,points.begin(), points.end(), [](const Pt &pt){
					return json::Object{
						{"x",pt.x},{"y",pt.y},{"h",pt.h},{"b",pt.b}
					};
				})},
				{"neutral", neutral_price},
				{"tangent",tangent},
				{"current",json::Object{
					{"x",minfo.invert_price?1.0/mst.cur_price:mst.cur_price},
					{"b",cp.equity},
					{"h",std::abs(cp.position*mst.cur_price)},
				}}
			};
		}


	}
	mst.minfo = &minfo;

	Object result;
	IBrokerControl *bcontrol = dynamic_cast<IBrokerControl *>(api.get());
	if (bcontrol) {
		auto binfo = bcontrol->getBrokerInfo();
		result.set("broker",brokerToJSON(broker_id, binfo, req->getRootPath()));
	}

	open_orders = api->getOpenOrders(pair_id);


	result.set("pair", pair_id);
	result.set("orders", json::Value(json::array, open_orders.begin(), open_orders.end(),[](const IStockApi::Order &ord) {
		return ord.toJSON();
	}));
	result.set("visstrategy",visstrategy);
	result.set("strategy_raw_state", strategy_raw_state);
	result.set("spread_raw_state", spread_raw_state);
	result.set("strategy_state", strategy_state);
	result.set("safe_range", json::Object {
		{"min", safe_range.min},
		{"max", safe_range.max}
	});
	result.set("market_info", minfo.toJSON());
	result.set("state", json::Object {
		{"balance",mst.balance},
		{"cur_leverage",mst.cur_leverage},
		{"cur_price",mst.event_price},
		{"time",mst.event_time},
		{"equity",mst.event_equity},
		{"event",strMarketEvent[mst.event]},
		{"bid",mst.highest_buy_price},
		{"ask",mst.lowest_sell_price},
		{"last_trade",json::Object{
			{"price",mst.last_trade_price},
			{"size",mst.last_trade_size},
		}},
		{"available",json::Object {
			{"assets",mst.live_assets},
			{"currencies",mst.live_currencies},
		}},
		{"open_price",mst.open_price},
		{"position",mst.position},
		{"rpnl",mst.rpnl},
		{"spread_buy_price",mst.sug_buy_price},
		{"spread_sell_price",mst.sug_sell_price},
		{"upnl",mst.upnl},
		{"neutral_price",neutral_price?json::Value(neutral_price):json::Value()},
		{"equilibrium",eq},
		{"equity_allocation",eq_alloc},
		{"partial", json::Object {
			{"trades",partial_executions},
			{"position_offset",partial_offset.getPos()},
			{"open_price",partial_offset.getOpen()},
			{"target_buy",target_buy?Value(target_buy+mst.position):Value(nullptr)},
			{"target_sell",target_buy?Value(target_sell+mst.position):Value(nullptr)},
		}},
		{"achieve",achieve.has_value()?json::Value(json::Object({
				{"position", achieve->position},
				{"balance", achieve->balance},
				{"remain", achieve->position - mst.position}
		})):json::Value(nullptr)}
	});
	result.set("live", json::Object {
		{"assets", live_assets},
		{"currencies", live_currency},
	});
	result.set("exists", exists);
	result.set("stopped", stopped);
	result.set("uid", uid);
	send_json(req,result);
}

bool HttpAPI::get_api_wallet(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_view)) return true;
	auto wallet = core->get_traders().lock_shared()->get_wallet_db();
	if (wallet == nullptr) {
		send_json(req, json::array);
		return true;
	}
	auto lkwallet = wallet.lock_shared();
	auto lkcache = core->get_balance_cache().lock_shared();


	auto data = lkwallet->getAggregated();
	json::Value allocations (json::array, data.begin(), data.end(), [&](const WalletDB::AggrItem &x) {
		Object mdata;
		mdata.set("broker", x.broker);
		mdata.set("wallet", x.wallet);
		mdata.set("symbol", x.symbol);
		mdata.set("allocated", x.val);
		mdata.set("balance", lkcache->get(x.broker, x.wallet, x.symbol));
		return json::Value(mdata);
	});

	send_json(req, allocations);
	return true;

}

bool HttpAPI::get_api_utilization(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_view)) return true;
	HeaderValue tm = v["tm"];
	PUtilization utlz = core->get_utlz();
	json::Value res = utlz.lock_shared()->getUtilization(tm.getUInt());
	send_json(req, res);
	return true;
}

bool HttpAPI::get_api_trader(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_view)) return true;
	auto trs = core->get_traders().lock_shared();
	send_json(req, json::Value(json::array, trs->begin(), trs->end(),[](const auto &x){
		return x.first;
	}));
	return true;
}

bool HttpAPI::get_api_trader_trader(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_view)) return true;
	auto trls = core->get_traders().lock_shared();
	PTrader tr = trls->get_trader(v["trader"]);
	if (tr != nullptr) {
		trader_info(req, v["trader"],"","",0, v["vis"] == "true");
	} else {
		api_error(req, 404, "Trader not found");
	}
	return true;
}

bool HttpAPI::get_api_broker_pairs_traderinfo(Req &req, const Args &args) {
	if (!check_acl(req, ACL::config_view)) return true;
	std::string_view broker = args["broker"];
	std::string_view pair = args["pair"];
	unsigned int swap_mode = args["swap_mode"].getUInt();

	trader_info(req, "", broker,pair, swap_mode, false);
	return true;

}

bool HttpAPI::delete_api_trader_trader(Req &req, const Args &v) {
	if (!check_acl(req, ACL::config_edit)) return true;
	auto trls = core->get_traders().lock_shared();
	PTrader tr = trls->get_trader(v["trader"]);
	if (tr != nullptr) {
		tr.lock()->stop();
		req->setStatus(202);
		send_json(req, true);
	} else {
		api_error(req, 404, "Trader not found");
	}
	return true;

}

bool HttpAPI::get_api_trader_trading(Req &req, const Args &v) {
	if (!check_acl(req, ACL::manual_trading)) return true;
	PTrader tr = core->get_traders().lock_shared()->get_trader(v["trader"]);
	if (tr == nullptr) {
		api_error(req, 404, "Trader not found");
		return true;
	}
	auto trl = tr.lock_shared();


	const auto &chartx = trl->get_minute_chart();
	auto data = chartx->read(600);

	PStockApi broker = trl->get_exchange();
	std::unique_lock  lk(core->get_cycle_lock(),std::try_to_lock);
	if (lk.owns_lock()) {
		broker->reset(std::chrono::system_clock::now());
	}
	Object out;
	out.set("chart", Value(json::array,data.begin(), data.end(),[&](const auto &item) {
		return json::Value({item.timestamp,item.price});
	}));
	std::size_t start = data.empty()?0:data[0].timestamp;
	auto trades = trl->get_trades();
	out.set("trades", Value(json::array, trades.begin(), trades.end(),[&](auto &&item) {
		if (item.time >= start) return item.toJSON(); else return Value();
	}));
	auto cfg = trl->get_config();
	auto ticker = broker->getTicker(cfg.pairsymb);
	auto orders = broker->getOpenOrders(cfg.pairsymb);
	auto minfo = trl->get_market_info();
	out.set("ticker", Object({{"ask", ticker.ask},{"bid", ticker.bid},{"last", ticker.last},{"time", ticker.time}}));
	out.set("orders", Value(json::array, orders.begin(), orders.end(), [](const auto &x){return x.toJSON();}));
	out.set("broker", cfg.broker);
	out.set("minfo", minfo.toJSON());
	out.set("assets", broker->getBalance(minfo.asset_symbol,cfg.pairsymb));
	out.set("currency", broker->getBalance(minfo.currency_symbol,cfg.pairsymb));
	send_json(req, out);
	return true;

}

bool HttpAPI::post_api_trader_trading(Req &req, const Args &v) {
	if (!check_acl(req, ACL::manual_trading)) return true;
	PTrader tr = core->get_traders().lock_shared()->get_trader(v["trader"]);
	if (tr == nullptr) {
		api_error(req, 404, "Trader not found");
		return true;
	}
	req->readBody(req, max_upload) >> [tr, me=PHttpAPI(this)](Req &req, std::string_view body) {
		try {
			auto trl = tr.lock_shared();
			auto brk = trl->get_exchange();
			auto cfg = trl->get_config();

			Value parsed = Value::fromString(body);
			Value price = parsed["price"];
			if (price.type() == json::string) {
				std::unique_lock lk(me->core->get_cycle_lock(), std::try_to_lock);
				if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
				auto ticker = brk->getTicker(cfg.pairsymb);
				if (price.getString() == "ask")
					price = ticker.ask;
				else if (price.getString() == "bid")
					price = ticker.bid;
				else {
					me->api_error(req, 400, "Invalid price (bid or ask or number)");
					return;
				}
			}
			Value res = brk->placeOrder(cfg.pairsymb,
							parsed["size"].getNumber(),
							price.getNumber(), json::Value(),
							parsed["replace_id"],0);
			send_json(req, res);
		} catch (AbstractExtern::Exception &e) {
			if (e.isResponse()) api_error(req,409,e.what());
			else throw;
		} catch (ParseError &e) {
			api_error(req, 400, e.what());
		}
	};
	return true;

}

bool HttpAPI::delete_api_trader_trading(Req &req, const Args &v) {
	if (!check_acl(req, ACL::manual_trading)) return true;
	PTrader tr = core->get_traders().lock_shared()->get_trader(v["trader"]);
	if (tr == nullptr) {
		api_error(req, 404, "Trader not found");
		return true;
	}
	req->readBody(req, max_upload) >> [tr, me=PHttpAPI(this)](Req &req, std::string_view body) {
		try {
			auto trl = tr.lock_shared();
			auto brk = trl->get_exchange();
			auto cfg = trl->get_config();

			Value parsed = body.empty()?Value(json::object):Value::fromString(body);
			Value id = parsed["id"];
			if (id.hasValue()) {
				brk->placeOrder(cfg.pairsymb,0,0,json::Value(),id,0);
			} else {
				std::unique_lock lk(me->core->get_cycle_lock(), std::try_to_lock);
				if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
				IStockApi::Orders ords = brk->getOpenOrders(cfg.pairsymb);
				IStockApi::OrderList olist;
				for (const auto &o : ords) {
					olist.push_back({
						cfg.pairsymb, 0,0,json::Value(),o.id,0
					});
				}
				IStockApi::OrderIdList rets;
				IStockApi::OrderPlaceErrorList errs;
				brk->batchPlaceOrder(olist,rets,errs);
			}
			me->send_json(req, true);
		} catch (ParseError &e) {
			me->api_error(req,400, e.what());
		} catch (AbstractExtern::Exception &e) {
			if (e.isResponse()) me->api_error(req,409,e.what()); else throw;
		}
	};
	return true;

}

bool HttpAPI::get_api_backtest_trader_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	bool withtm = v["withtm"] == "true";
	PTrader tr = core->get_traders().lock_shared()->get_trader(v["trader"]);
		if (tr == nullptr) {
			api_error(req, 404, "Trader not found");
			return true;
		}
	auto trl = tr.lock_shared();
	const auto &dt = trl->get_minute_chart();
	req->setContentType(ctx_json);
	json::Value out = ([&]{
		auto rd = dt->read(-1);

		return withtm?json::Value(json::array, rd.begin(), rd.end(), [&](const auto &x){
			return json::Value({x.timestamp, x.price});
		}):json::Value(json::array, rd.begin(), rd.end(), [&](const auto &x){
			return json::Value(x.price);
		});
	})();

	send_json(req, out);
	return true;
}

bool HttpAPI::post_api_backtest_trader_data(Req &req, const Args &v) {
	if (!check_acl(req, ACL::backtest)) return true;
	PTrader tr = core->get_traders().lock_shared()->get_trader(v["trader"]);
		if (tr == nullptr) {
			api_error(req, 404, "Trader not found");
			return true;
		}
	auto trl = tr.lock_shared();
	const auto &dt = trl->get_minute_chart();
	auto bts = core->get_backtest_storage().lock();
	json::Value out = ([&]{
		auto rd = dt->read(-1);
		return json::Value(json::array, rd.begin(), rd.end(), [&](const auto &x){
			return json::Value(x.price);
		});
	})();
	std::string id = bts->store_data(out);
	req->setStatus(201);
	req->set("Location",std::string(req->getRootPath()).append("/api/backtest/data/").append(id));
	send_json(req, json::Object({"id", id}));
	return true;

}
