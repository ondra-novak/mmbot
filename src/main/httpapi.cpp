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

#include "../brokers/httpjson.h"
#include "backtest2.h"

#include "istrategy3.h"

#include "trader_factory.h"

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

static std::initializer_list<OpenAPIServer::SchemaItem> marketInfo={
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
		{"wallet_id", "string","id of wallet used for trading"}};

static std::initializer_list<OpenAPIServer::SchemaItem> tickerStruct={
		{"ask","number"},{"bid","number"},{"last","number"},{"time","number"}
};

static OpenAPIServer::ParameterObject brokerId = {"broker","path","string","Broker's ID"};
static OpenAPIServer::ParameterObject pairId = {"pair","path","string","Pair ID"};


static std::string ctx_json = "application/json";

void HttpAPI::init(std::shared_ptr<OpenAPIServer> server) {
	cur_server = server;
	PHttpAPI me = this;

	auto reg = [&](const std::string_view &p) {return server->addPath(p);};

	server->addPath("",[me](Req &req, const std::string_view &vpath){ return me->get_root(req, vpath);});

	reg("/set_cookie")
		.POST("General","Set cookie", "Content is application/x-www-form-urlencoded, and expects auth=<content>. It directly copies this to the cookie (as session) - this is intended to be used by platform to allow direct entering to the administration using JWT token",{},"",{
				{"application/x-www-form-urlencoded","","object","",{
						{"auth","string","content of cookie"},
						{"redir","string","http URL where to redirect after cookie is set. If not specified, request returns 202",{},true}}},
			} ).method(me,&HttpAPI::post_set_cookie);
	reg("/api/_up")
		.GET("Monitoring","Returns 200 if server is running").method(me,&HttpAPI::get_api__up);
	reg("/api/data")
		.GET("Statistics","Statistics datastream SSE","",{},{
			{200,"SSE Stream",{
					{"text/event-stream","Event stream","string","Each line starts with data: followed with JSON event (see detailed documentation)"}
			}}}).method(me,&HttpAPI::get_api_data);
	reg("/api/report.json")
		.GET("Statistics","Statistics report snapshot").method(me,&HttpAPI::get_api_report_json);
	reg("/api/login")
		.GET("General","Login using Basic Autentification","Requests for login dialog and after login is successful, redirects the user agent to the specified url",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_login);
	reg("/api/logout")
		.GET("General","Clears basic autentification state","It also deletes auth cookie to ensure that logout works",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_logout);
	reg("/api/user")
		.GET("General","Get information about current user","",{},{
					{200,"Information about user",{
						{ctx_json,"UserInfo","object","",{
								{"user","string","Name of the user (username)"},
								{"exists","boolean","true=User is valid, false=user is not valid"},
								{"jwt","boolean","true=logged in from external login server using JWT token"},
								{"viewer","boolean","true=current user can read statistics"},
								{"report","boolean","true=current user can read trading reports"},
								{"admin_view","boolean","true=current user can read settings, but cannot change them"},
								{"admin_edit","boolean","true=current user can read and edit settings"},
								{"backtest","boolean","true=current user can execute backtest"}
						}}
					}}
			}).method(me,&HttpAPI::get_api_user)
		.POST("General","Log-in a user","",{
					{"auth","cookie","string","JWT token",{}, false},
					{"Authorization","header","string","Bearer JWT token/Basic authorization",{}, false}
			},"",{
					{ctx_json,"","object","User credentials. The credentials can be also send as cookie 'auth' (JWT) or as Basic authentification (Authorization)",{
						{"user","string","Username to log-in",{},true},
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
								{"backtest","boolean","true=current user can execute backtest"},
								{"cookie","string","returned cookie",{},true}
						}}
					}}
			}).method(me,&HttpAPI::post_api_user)
		.DELETE("General","Log-out the current user")
				.method(me,&HttpAPI::delete_api_user);

	reg("/api/user/set_password")
		.POST("General","Change password of current user", "", {},"",{
				{ctx_json,"","object","",{
					{"old","string","old password"},
					{"new","string","new password"},
				}},
			},{
				{202,"Password set",{{ctx_json,"","boolean","true"}}},
				{400,"New password can't be empty / No change (same password)"},
				{409,"Current password doesn't match"},
			}).method(me, &HttpAPI::get_api_user_set_password);
	reg("/api/run/{id}")
		.GET("General","Retrieve SSE stream of long running operation - some operations need to open this stream to actually be executed","",{
			{"id","path","integer","Operation ID, it is returned as result of operation creation"}},{
			{200,"Progress state", {
					{"application/json-seq","","string","line separated JSONs"},
					{"text/event-stream","","string","JSON as event stream, prefixed by \"data:\" "}
			}},
				{404,"ID not found"}
			}).method(me, &HttpAPI::get_api_run)
		.DELETE("General","Cancel running operation graciously (you can cancel operation by closing SSE, but you will not able to receive final status)","",{
			{"id","path","integer","Operation ID, it is returned as result of operation creation"}},"",{},{
					{200,"Operation flagged to cancel"},
					{404,"Operation ID not found"}
			}).method(me,&HttpAPI::delete_api_run);

	reg("/api/admin/broker")
		.GET("Brokers","List of configured brokers","",{},{
			{200,"",{{ctx_json,"","array","",{brokerFormat}}}}}).method(me, &HttpAPI::get_api_admin_broker)
		.DELETE("Brokers","Reload brokers","Terminates all brokers and reloads each when it is accessed. This operation should not be considered as harmful and can solve any temporary issue with a broker",{},"",{},{
			{202,"Accepted",{}}}).method(me, &HttpAPI::delete_api_admin_broker);


	reg("/api/admin/broker/{broker}/icon.png")
		.GET("Brokers","Broker's favicon","",{brokerId},
			{{200,"OK",{{"image/png","","","Broker's favicon"}}}}).method(me, &HttpAPI::get_api_admin_broker_icon_png);

	reg("/api/admin/broker/{broker}/licence")
		.GET("Brokers","Licence file","",{brokerId},
			{{200,"OK",{{ctx_json,"","string","Licence text"}}}}).method(me, &HttpAPI::get_api_admin_broker_licence);

	reg("/api/admin/broker/{broker}/apikey")
		.GET("Brokers","Retrieve APIKEY format","",{brokerId},
			{{200,"OK",{{ctx_json,"","array","List of field",{stdForm}}}}}).method(me, &HttpAPI::get_api_admin_broker_apikey)
		.PUT("Brokers","Set APIKEY","",{brokerId},"",{
			{OpenAPIServer::MediaObject{"application/json","apikey","assoc","key:value of apikey",{
					{"","anyOf","key:value",{{"","string"},{"","number"},{"","boolean"}}}
			}}}},{
			{OpenAPIServer::ResponseObject{200,"OK",{{ctx_json,"","boolean","true"}}}},
			{OpenAPIServer::ResponseObject{409,"Conflict (invalid api key)",{{ctx_json,"","string","Error message returned by exchange API server"}}}}
			}).method(me, &HttpAPI::put_api_admin_broker_apikey);

	reg("/api/admin/broker/{broker}/pairs")
		.GET("Brokers","List of available pairs","",{brokerId,{"flat","query","boolean","return flat structure (default false)",{},false}},
			{{200,"OK",{{ctx_json,"","oneOf","depend on 'flat' argument",{
					{"","assoc","Structure (flat=false)",{{"","string","pairid"}}},
					{"","array","Flat (flat=true",{{"","string","pairid"}}}
			}}}}}).method(me, &HttpAPI::get_api_admin_broker_pairs);

	reg("/api/admin/broker/{broker}/pairs/{pair}")
		.GET("Brokers","Market information","",{brokerId,pairId},{
				{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "object", "", marketInfo}}}}
				}).method(me, &HttpAPI::get_api_admin_broker_pairs_pair);

	reg("/api/admin/broker/{broker}/pairs/{pair}/ticker")
		.GET("Brokers","Get ticker","",{brokerId,pairId},{
			{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "object", "ticker information",tickerStruct}}}}
			}).method(me, &HttpAPI::get_api_admin_broker_pairs_ticker);

	reg("/api/admin/broker/{broker}/settings")
		.GET("Brokers","Get broker settings (form)","",{brokerId,{"pair","query","string","selected pair"}},{
				{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "array", "Form fields",{stdForm}}}}}
			}).method(me, &HttpAPI::get_api_admin_broker_pairs_settings)
		.PUT("Brokers","Store broker settings","",{brokerId},"",{
			{OpenAPIServer::MediaObject{"application/json","apikey","assoc","key:value",{
					{"","anyOf","key:value",{{"","string"},{"","number"},{"","boolean"}}}
			}}}},{
			{OpenAPIServer::ResponseObject{200,"OK",{{ctx_json,"","object","all settings"}}}},
			}).method(me, &HttpAPI::put_api_admin_broker_pairs_settings);;

	reg("/api/admin/broker/{broker}/pairs/{pair}/orders")
		.GET("Brokers","Get open orders","",{brokerId,pairId},{
			{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "array", "opened orders",{}}}}}
			}).method(me, &HttpAPI::get_api_admin_broker_pairs_orders)
		.POST("Brokers","Create order","",{brokerId,pairId},"",{
			{OpenAPIServer::MediaObject{ctx_json,"","object","Order parameters",{
				{"price","oneOf","Order price",{
						{"","enum","Ask or bid",{{"ask"},{"bid"}}},
						{"","number","price"}
				}},
				{"size","number","Order size"},
				{"replace_id","","ID of order to replace",{},true},
			}}}
			},{{201,"Order created",{{ctx_json,"","string","New order ID"}}}})
			.method(me,&HttpAPI::put_api_admin_broker_pairs_orders)
		.DELETE("Brokers","Cancel one or all orders","",{brokerId,pairId},"",{
				{ctx_json, "", "object","",{{"id","string","orderid",{},true}}},
				{"<empty>","Cancel all orders","string","Empty body"}},
				{{OpenAPIServer::ResponseObject{202,"Orders are deleted",{{ctx_json,"","boolean","true"}}}}
				}).method(me, &HttpAPI::delete_api_admin_broker_pairs_orders);


	reg("/api/admin/config")
		.GET("Configuration","Get configuration file","",{},{
			{200,"OK",{{ctx_json, "", "object"}}}}).method(me, &HttpAPI::get_api_admin_config)
		.POST("Configuration","Apply config diff, restart trading and return whole config","",{},"",{
			{ctx_json,"","object","Configuration diff (transfer only changed fields, use {} to delete fields)"}},
			{{202,"Accepted",{{ctx_json,"","object","whole merged configuration"}}}
			}).method(me, &HttpAPI::post_api_admin_config);
	reg("/api/admin/forms")
		.GET("Configuration","Retrieve form definition for specified object","",{},
			{{200,"OK",{{ctx_json,"","object"}}}}).method(me, &HttpAPI::get_api_admin_form);

	reg("/api/backtest/data/{id}")
		.GET("Backtest","Retrieve minute price data (data must be uploaded, or imported from exchange)","",{
				{"id","path","identifier"}
			},{
					{200,"Stored data",{{ctx_json,"","array","",{{"number","number"}}}}},
					{404,"ID is not valud - need reupload"}
			}).method(me, &HttpAPI::get_api_backtest_data);
	reg("/api/backtest/data")
		.POST("Backtest","Upload minute price data - one item per minute","",{},"",{
				{ctx_json,"data","array","Minute data - numbers, one number per minute",{{"","number","",}}},
			},{
				{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}}
			}).method(me, &HttpAPI::post_api_backtest_data);
	reg("/api/backtest/historical_data")
		.GET("Backtest","Retrieve available symbols for historical data","",{},{
				{200,"OK",{{ctx_json,"","array","",{{"string","string"}}}}}
			}).method(me, &HttpAPI::get_api_backtest_historical_data)
		.POST("Backtest","Upload historical data (from historical service)","",{},"",{
				{ctx_json,"","object","",{
						{"asset","string","asset"},
						{"currency","string","currency"},
						{"smooth","integer","smooth"}
				}}
			},{
				{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}}
			}).method(me, &HttpAPI::post_api_backtest_historical_data);
	reg("/api/backtest/historical_data/{broker}/{pair}")
		.GET("Backtest","Probe, whether historical data are available for given pair","",{brokerId,pairId},{
			{200,"Data are available",{{ctx_json,"","boolean","true"}}},
			{404,"Data are not available"}
		}).method(me, &HttpAPI::get_api_backtest_historical_data_broker_pair)
		.POST("Backtest","Download historical data from exchange","",{brokerId,pairId},"",{},{
			{202,"Download in progres",{{ctx_json,"","object","Information about process",{
					{"progress","string", "ID of progress (/api/progress/ID). When URL becomes unavalable, the downloaded data are ready"},
					{"id","string", "ID under which data becomes available"}
			}}}},
			{404,"Data are not available, cannot be downloaded"}
		}).method(me, &HttpAPI::post_api_backtest_historical_data_broker_pair);

	reg("/api/backtest")
		.POST("Backtest","Create new backtest. ID of test returned and can be executed through /api/run/<id>","",{
			{"stream","query","boolean","Prepare operation to by streamed as executed (SSE) - default is false - result is returned directly to the response"},
			{"Accept","header","enum","Requested conten type",{{"text/event-stream"},{"application/json-seq"}}}
		},"",{
				{ctx_json, "config","object","backtest configuration",{
						{"makret","object","Market information",marketInfo},
						{"source","object","source definition",{
								{"id","string","Data source - ID of uploaded minute data"},
								{"offset","integer","(optional) skip specified count of minutes"},
								{"limit","integer","(optional) limit count of minutes"},
								{"start_time","int64","(optional) javascript timestamp (milliseconds) of starting date. If not set, current date is used"},
								{"init_price","number","(optional) initial price, if not set (or zero), it uses prices from supplied minute data"},
								{"backward","boolean","(optional) process data backward"},
								{"invert","boolean","(optional) invert data - swap top and bottom"},
								{"mirror","boolean","(optional) extends data with mirrored version of itself"}
						}},
						{"trader","object","Trader configuration (complete) - see other doc"},
						{"init","object","Initial conditions",{
								{"position","number","initial position. If not set, recommended position is used"},
								{"balance","number","initial currency balance. Mandatory"}
						}},
						{"output","object","Specify additional output",{
								{"freq","enum","how often is output generated (default is trade)",{{"trade"},{"minute"}}},
								{"strategy_state","boolean","include state of strategy (defaulf is false)"},
								{"log","boolean","include log messages (default is false)"},
								{"strategy_raw_state","boolean","include raw state of strategy (default is false)"},
								{"spread_raw_state","boolean","include raw state of spread (default is false)"},
								{"orders","boolean","include open orders (default false, for freq=minute)"},
								{"stats","boolean","include current position, balance, profit, norm-profit, etc (default false)"},
								{"debug","boolean","include more verbose (possible duplicated) informations (default is false)"},
								{"diff","boolean","send diff - each event is diff-object to previous event default is false)"},
						}}

				}}},{
						{200,"OK",{{"text/event-stream","","","Events"},{"application/json-seq","","","Events"}}},
						{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}},
						{400,"Bad request - syntaxt errors"},
						{409,"Conflict - formal errors"},
						{416,"Invalid range: offset and limit out of range"},
				}).method(me, &HttpAPI::post_api_backtest);


	server->addSwagBrowser("/swagger");

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
		api_error(req,404);
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
		api_error(req,404);
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
		api_error(req,404);
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

static json::Value ordersToJSON(const IStockApi::Orders &ords) {
	return Value(json::array, ords.begin(), ords.end(),
			[&](const IStockApi::Order &ord) {
				return Object({{"price", ord.price},{
						"size", ord.size},{"clientId",
						ord.client_id},{"id", ord.id}});
			});
}

bool HttpAPI::get_api_admin_broker_pairs_ticker(Req &req, const Args &args) {
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

bool HttpAPI::get_api_admin_broker_pairs_settings(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_view)) return true;
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

bool HttpAPI::put_api_admin_broker_pairs_settings(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_edit)) return true;
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

bool HttpAPI::get_api_admin_broker_pairs_orders(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_view)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string_view pair = args["pair"];
	if (brk != nullptr) {
		std::unique_lock lk(core->get_cycle_lock(),std::try_to_lock);
		try {
			if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
			send_json(req, ordersToJSON(brk->getOpenOrders(pair)));
		} catch (AbstractExtern::Exception &e) {
			if (e.isResponse()) api_error(req,404, e.what()); else throw;
		}
	} else {
		api_error(req,404, "Broker not found");
	}
	return true;
}

bool HttpAPI::put_api_admin_broker_pairs_orders(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_edit)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string pair ( args["pair"]);
	if (brk != nullptr) {
		req->readBody(req, max_upload) >> [brk,pair, me=PHttpAPI(this)](Req &req, std::string_view body) {
			try {
				Value parsed = Value::fromString(body);
				Value price = parsed["price"];
				if (price.type() == json::string) {
					std::unique_lock lk(me->core->get_cycle_lock(), std::try_to_lock);
					if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
					auto ticker = brk->getTicker(pair);
					if (price.getString() == "ask")
						price = ticker.ask;
					else if (price.getString() == "bid")
						price = ticker.bid;
					else {
						me->api_error(req, 400, "Invalid price (bid or ask or number)");
						return;
					}
				}
				Value res = brk->placeOrder(pair,
								parsed["size"].getNumber(),
								price.getNumber(), json::Value(),
								parsed["replace_id"],0);
				me->send_json(req, res);

			} catch (ParseError &e) {
				me->api_error(req,400, e.what());
			} catch (AbstractExtern::Exception &e) {
				if (e.isResponse()) me->api_error(req,409,e.what()); else throw;
			}
		};
	} else {
		api_error(req,404, "Broker not found");
	}
	return true;
}


bool HttpAPI::delete_api_admin_broker_pairs_orders(Req &req, const Args &args) {
	if (!check_acl(req, ACL::admin_edit)) return true;
	auto brk = core->get_broker_list()->get_broker(args["broker"]);
	std::string pair ( args["pair"]);
	if (brk != nullptr) {
		req->readBody(req, max_upload) >> [brk,pair, me=PHttpAPI(this)](Req &req, std::string_view body) {
			try {
				Value parsed = body.empty()?Value(json::object):Value::fromString(body);
				Value id = parsed["id"];
				if (id.hasValue()) {
					brk->placeOrder(pair,0,0,json::Value(),id,0);
				} else {
					std::unique_lock lk(me->core->get_cycle_lock(), std::try_to_lock);
					if (lk.owns_lock()) brk->reset(std::chrono::system_clock::now());
					IStockApi::Orders ords = brk->getOpenOrders(pair);
					IStockApi::OrderList olist;
					for (const auto &o : ords) {
						olist.push_back({
							pair, 0,0,json::Value(),o.id,0
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
	} else {
		api_error(req,404, "Broker not found");
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

bool HttpAPI::get_api_admin_config(Req &req, const Args &) {
	if (!check_acl(req, ACL::admin_view)) return true;
	send_json(req, core->get_config());
	return true;
}


bool HttpAPI::post_api_admin_config(Req &req, const Args &v) {
	req->readBody(req, max_upload) >> [me = PHttpAPI(this)](Req &req, const std::string_view &text) {
		if (!me->check_acl(req, ACL::admin_edit)) return ;
		try {
			std::lock_guard _(me->cfg_lock);
			Value cfg_diff = Value::fromString(text);
			Value cfg = me->core->get_config();
			Value cfg_merged = merge_JSON(cfg, cfg_diff);
			Value users = cfg_merged["users"];
			if (users.defined()) {
				users = AuthService::conv_pwd_to_hash(users);
				cfg_merged.setItems({{"users",users}});
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

bool HttpAPI::get_api_admin_form(Req &req, const Args &v) {
	Object forms;

	auto append_tool = [&](std::string_view id, const auto &tool) {
		forms.set(id, Value(json::array, tool.begin(), tool.end(), [](const auto &x){
			return tooldesc_to_JSON(x.second->get_description());
		}));
	};

	append_tool("strategies", StrategyRegister::getInstance());
	append_tool("spread_generators", SpreadRegister::getInstance());
	forms.set("trader", get_trader_form());

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
	if (!check_acl(req, ACL::admin_view)) return true;
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
