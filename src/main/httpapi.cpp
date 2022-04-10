/*
 * httpapi.cpp
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#include <imtjson/serializer.h>
#include <imtjson/parser.h>
#include <imtjson/object.h>
#include "httpapi.h"

#include "../brokers/httpjson.h"
#include "abstractExtern.h"

#include "ssestream.h"

using namespace json;
using namespace userver;

static OpenAPIServer::SchemaItem brokerFormat ={
		"broker","object","broker description",{
				{"name","string","broker's id"},
				{"trading_enabled","boolean","Broker is configured, trading is enabled"},
				{"exchangeName","string","Name of exchange"},
				{"exchangeURL","string","Link to exchange (link to 'create account' page with referral"},
				{"version","string","version string"},
				{"subaccounts","boolean","true=broker supports subaccounts"},
				{"subaccount","string","Subaccount name (empty if not subaccount)"},
				{"nokeys","boolean","true=no keys are needed (probably simulator)"},
				{"settings","boolean","true=broker supports additional settings for each pair"},
		}
};

static OpenAPIServer::SchemaItem stdForm ={
		"field","object","field definition",{
				{"name","string","name of field"},
				{"label","string","Field label"},
				{"type","enum","Field type",{
						{"string"},{"number"},{"textarea"},{"enum"},{"label"},{"slider"},{"checkbox"}
				}},
				{"default","string","prefilled value (default)"},
				{"showif","object","defines conditions, when this field is shown",{
						{"string","assoc","name of field: list of values",{{"","array","list of values",{}}}}
				},true},
				{"hideif","object","defines conditions, when this field is hidden",{
						{"string","assoc","name of field: list of values",{{"","array","list of values",{}}}}
				},true},
				{"options","assoc","Options for enum <value>:<label>",{{"string"}}},
				{"min","number","minimal allowed value",{},true},
				{"max","number","maximal allowed value",{},true},
				{"step","number","value step",{},true},
				{"decimals","number","decimal numbers",{},true},
		}
};


static std::string ctx_json = "application/json";

void HttpAPI::init(std::shared_ptr<OpenAPIServer> server) {
	cur_server = server;
	PHttpAPI me = this;
	server->addPath("",[me](Req &req, const std::string_view &vpath){ return me->get_root(req, vpath);});
	server->addPath("/set_cookie").POST("General","Set cookie", "Content is application/x-www-form-urlencoded, and expects auth=<content>. It directly copies this to the cookie (as session) - this is intended to be used by platform to allow direct entering to the administration using JWT token",{},"",{
				{"application/x-www-form-urlencoded","","object","",{
						{"auth","string","content of cookie"},
						{"redir","string","http URL where to redirect after cookie is set. If not specified, request returns 202",{},true}}},
			} )
			.method(me,&HttpAPI::post_set_cookie);
	server->addPath("/api/_up").GET("Monitoring","Returns 200 if server is running")
			.method(me,&HttpAPI::get_api__up);
	server->addPath("/api/data").GET("Statistics","Statistics datastream SSE")
			.method(me,&HttpAPI::get_api_data);
	server->addPath("/api/report.json").GET("Statistics","Statistics report snapshot")
			.method(me,&HttpAPI::get_api_report_json);
	server->addPath("/api/login").GET("General","Login using Basic Autentification","Requests for login dialog and after login is successful, redirects the user agent to the specified url",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_login);
	server->addPath("/api/logout").GET("General","Clears basic autentification state","It also deletes auth cookie to ensure that logout works",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_logout);
	server->addPath("/api/user")
			.GET("General","Get information about current user","",{},{
					{200,"Information about user",{
						{ctx_json,"UserInfo","object","",{
								{"user","string","Name of the user (username)"},
								{"exists","boolean","true=User is valid, false=user is not valid"},
								{"jwt","boolean","true=logged in from external login server using JWT token"},
								{"viewer","boolean","true=current user can read statistics"},
								{"report","boolean","true=current user can read trading reports"},
								{"admin_view","boolean","true=current user can read settings, but cannot change them"},
								{"admin_edit","boolean","true=current user can read and edit settings"}
						}}
					}}
			})
				.method(me,&HttpAPI::get_api_user)
			.POST("General","Log-in a user","",{
					{"auth","cookie","string","JWT token",{}, false},
					{"Authorization","header","string","Bearer JWT token/Basic authorization",{}, false}
			},"",{
					{ctx_json,"","object","User credentials. The credentials can be also send as cookie 'auth' (JWT) or as Basic authentification (Authorization)",{
						{"username","string","Username to log-in",{},true},
						{"password","string","Password",{},true},
						{"token","string","A valid JWT token (or session)",{},true},
						{"exp","unsigned","Session expiration. If not specified, creates unspecified long session (permanent login)",{},true},
						{"cookie","enum","Specifies how to store result",{
								{"temporary","","Create temporary (session) cookie "},
								{"permanent","","Create permanent cookie"},
								{"return","","Don't set cookie, return result"},
						}},
						{"needauth","boolean","Require login using Basic auth, if no credentials are supplied (return 401)",{},true},
					}}
			},{
					{200,"Information about user",{
						{ctx_json,"UserInfo","object","",{
								{"user","string","Name of the user (username)"},
								{"exists","boolean","true=User is valid, false=user is not valid"},
								{"jwt","boolean","true=logged in from external login server using JWT token"},
								{"viewer","boolean","true=current user can read statistics"},
								{"report","boolean","true=current user can read trading reports"},
								{"admin_view","boolean","true=current user can read settings, but cannot change them"},
								{"admin_edit","boolean","true=current user can read and edit settings"},
								{"coolie","string","returned cookie",{},true}
						}}
					}}
			})
				.method(me,&HttpAPI::post_api_user)
			.DELETE("General","Log-out the current user")
				.method(me,&HttpAPI::delete_api_user);

	server->addPath("/api/progress/{id}").GET("General","Retrieve operation progress (for long running operations)","",{
			{"id","path","integer","Progress identifier"}
	},{
			{200,"Progress state", {
					{ctx_json,"","object","",{
							{"value","number","Progress value in percent"},
							{"desc","string","optional text describing current operation"},
					}}}},
			{410,"ID not found (operation finished)", {
					{ctx_json,"","null","",}
			}}
	}).method(me, &HttpAPI::get_api_progress);

	server->addPath("/api/admin/broker").GET("Brokers","List of configured brokers","",{},{
			{200,"",{{ctx_json,"","array","",{brokerFormat}}}}
	}).method(me, &HttpAPI::get_api_admin_broker)
		.DELETE("Brokers","Reload brokers","Terminates all brokers and reloads each when it is accessed. This operation should not be considered as harmful and can solve any temporary issue with a broker",{},"",{},{
			{202,"Accepted",{}}
	}).method(me, &HttpAPI::delete_api_admin_broker);

	static OpenAPIServer::ParameterObject brokerId = {"broker","path","string","Broker's ID"};
	static OpenAPIServer::ParameterObject pairId = {"pair","path","string","Pair ID"};

	server->addPath("/api/admin/broker/{broker}/icon.png").GET("Brokers","Broker's favicon","",{brokerId},
			{{200,"OK",{{"image/png","","","Broker's favicon"}}}}
	).method(me, &HttpAPI::get_api_admin_broker_icon_png);

	server->addPath("/api/admin/broker/{broker}/licence").GET("Brokers","Licence file","",{brokerId},
			{{200,"OK",{{ctx_json,"","string","Licence text"}}}
	}).method(me, &HttpAPI::get_api_admin_broker_licence);

	server->addPath("/api/admin/broker/{broker}/apikey").GET("Brokers","Retrieve APIKEY format","",{brokerId},
			{{200,"OK",{{ctx_json,"","array","List of field",{stdForm}}}}
	}).method(me, &HttpAPI::get_api_admin_broker_apikey).PUT("Brokers","Set APIKEY","",{brokerId},"",{
			{OpenAPIServer::MediaObject{"application/json","apikey","assoc","key:value of apikey",{
					{"","anyOf","key:value",{{"","string"},{"","number"},{"","boolean"}}}
			}}}},{
			{OpenAPIServer::ResponseObject{200,"OK",{{ctx_json,"","boolean","true"}}}},
			{OpenAPIServer::ResponseObject{409,"Conflict (invalid api key)",{{ctx_json,"","string","Error message returned by exchange API server"}}}}
	}).method(me, &HttpAPI::put_api_admin_broker_apikey);

	server->addPath("/api/admin/broker/{broker}/pairs").GET("Brokers","List of available pairs","",{brokerId,{"flat","query","boolean","return flat structure (default false)",{},false}},
			{{200,"OK",{{ctx_json,"","oneOf","depend on 'flat' argument",{
					{"","assoc","Structure (flat=false)",{{"","string","pairid"}}},
					{"","array","Flat (flat=true",{{"","string","pairid"}}}
			}}}}
	}).method(me, &HttpAPI::get_api_admin_broker_pairs);

	server->addPath("/api/admin/broker/{broker}/pairs/{pair}").GET("Brokers","Market information","",{brokerId,pairId},{
		{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "object", "", {
				{"asset_step","number","amount granuality"},
				{"currency_step", "number", "tick size (price step)"},
				{"asset_symbol","string", "symbol for asset"},
				{"currency_symbol","string","symbol for currency"},
				{"min_size", "number", "minimal order amount"},
				{"min_volume", "number", "minimal order volume (amount x price)"},
				{"fees", "number", "fees as part of 1 (0.1%=0.001) "},
				{"feeScheme","number", "how fees are calculated (income, asset, currency)"},
				{"leverage", "number", "leverage amount, 0 = spot"},
				{"invert_price","boolean", "inverted market (price is inverted)" },
				{"inverted_symbol", "string","quoting symbol (inverted symbol)"},
				{"simulator", "boolean", "true if simulator"},
				{"private_chart", "boolean", "true if price is derived, and should not be presented as official asset price"},
				{"wallet_id", "string","id of wallet used for trading"}}}}}}
	}).method(me, &HttpAPI::get_api_admin_broker_pairs_pair);


	server->addSwagBrowser("/swagger");

	{
		auto p = progress.lock();
		int i = p->alloc();
		p->set_percent(i, 42.56);
		p->set_desc(i, "Test progress");
	}

}

bool HttpAPI::get_api__up(Req &req, const Args &args) {
	req->setContentType("text/plain;charset=utf-8");
	req->send("");
	return true;
}

bool HttpAPI::check_acl(Req &req, ACL acl) {
	auto auth = core->get_auth().lock();
	auto user = auth->get_user(*req);
	return auth->check_auth(user, acl, *req);

}


bool HttpAPI::get_api_data(Req &req, const Args &args) {
	if (!check_acl(req, ACL::viewer)) return true;
	auto rpt = core->get_report().lock();
	ondra_shared::RefCntPtr<SSEStream> sse = new SSEStream(std::move(req));
	sse->init();
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
	req->send("Logged in");
	return true;
}

bool HttpAPI::get_api_logout(Req &req, const Args &args) {
	std::string cookie = "auth=;Max-Age=0;Path=";
	cookie.append(req->getRootPath());
	req->set("Set-Cookie",cookie);

	HeaderValue auth = req->get("Authorization");
	HeaderValue redir = args[redir];
	if (auth.defined) {
		if (redir.defined) {
			req->set("Refresh", std::to_string(1).append(";").append(redir));
		}
		AuthService::basic_auth(*req);
	} else {
		req->set("Location", redir);
		req->setStatus(302);
		req->send("");
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
					cookie.append(req->getRootPath());
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
			req->sendErrorPage(400, e.what());
		} catch (json::ParseError &e) {
			req->sendErrorPage(400, e.what());
		} catch (AdminPartyException &e) {
			req->sendErrorPage(403, e.what());
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


bool HttpAPI::get_api_admin_broker(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_view)) return true;
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

bool HttpAPI::delete_api_admin_broker(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_view)) return true;
	core->get_broker_list()->enum_brokers([&](const std::string_view &name, const PStockApi &api){
		IBrokerInstanceControl *ex = dynamic_cast<IBrokerInstanceControl  *>(api.get());
		if (ex) ex->unload();
	});
	req->setStatus(202);
	send_json(req, true);
	return true;

}

bool HttpAPI::get_api_admin_broker_icon_png(Req &req, const Args &args) {
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

bool HttpAPI::get_api_admin_broker_licence(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto bc = dynamic_cast<IBrokerControl *>(brk.get());
	if (bc) {
		auto binfo = bc->getBrokerInfo();
		send_json(req, binfo.licence);
	} else {
		req->sendErrorPage(404);
	}
	return true;
}

bool HttpAPI::get_api_admin_broker_apikey(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_view)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	auto bc = dynamic_cast<IApiKey *>(brk.get());
	if (bc) {
		send_json(req,bc->getApiKeyFields());
	} else {
		req->sendErrorPage(404);
	}
	return true;
}

bool HttpAPI::put_api_admin_broker_apikey(Req &req, const Args &args) {
	req->readBody(req, max_upload) >> [me=PHttpAPI(this), broker = std::string(args["broker"])]
				(Req &req, const std::string_view &body) {
		if (!me->check_acl(req, ACL::admin_edit)) return;
		try {
			json::Value data = json::Value::fromString(body);
			auto brk = me->core->get_broker_list()->get_broker(broker);
			auto bc = dynamic_cast<IApiKey *>(brk.get());
			if (bc) {
				try {
					bc->setApiKey(data);
				} catch (const AbstractExtern::Exception &e) {
					if (e.isResponse()) {
						req->setStatus(409);
						me->send_json(req,e.what());
						return;
					} else {
						throw;
					}
				}
				me->send_json(req,true);
			} else {
				req->sendErrorPage(404);
			}
		} catch (ParseError &e) {
			req->sendErrorPage(400, e.what());
		}
	};
	return true;

}

bool HttpAPI::get_api_admin_broker_pairs(Req &req, const Args &args) {
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
		req->sendErrorPage(404);
	}
	return true;

}

bool HttpAPI::get_api_progress(Req &req, const Args &args) {
	auto res = progress.lock_shared()->get(args["id"].getUInt());
	if (res.has_value()) {
		send_json(req, Object {
			{"value", res->percent},
			{"desc", res->desc.empty()?Value():Value(res->desc)},
		});
	} else {
		req->setStatus(410);
		send_json(req, nullptr);
	}
	return true;
}

bool HttpAPI::get_api_admin_broker_pairs_pair(Req &req, const Args &args) {
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string_view pair = args["pair"];
	if (brk != nullptr) {
		try {
			IStockApi::MarketInfo minfo = brk->getMarketInfo(pair);
			send_json(req, minfo.toJSON());
		} catch (AbstractExtern::Exception &e) {
			req->sendErrorPage(404, e.what());
		}
	} else {
		req->sendErrorPage(404, "Broker not found");
	}
	return true;
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
