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


#include "ssestream.h"

void HttpAPI::init(std::shared_ptr<userver::OpenAPIServer> server) {
	cur_server = server;
	PHttpAPI me = this;
	server->addPath("",[me](Req &req, const std::string_view &vpath){ return me->get_root(req, vpath);});
	server->addPath("/set_cookie").POST("General","Set cookie", "Content is application/x-www-form-urlencoded, and expects auth=<content>. It directly copies this to the cookie (as session) - this is intended to be used by platform to allow direct entering to the administration using JWT token" )
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
			.GET("General","Get information about current user")
				.method(me,&HttpAPI::get_api_user)
			.POST("General","Log-in a user")
				.method(me,&HttpAPI::post_api_user)
			.DELETE("General","Log-out the current user")
				.method(me,&HttpAPI::delete_api_user);
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
	req->setContentType("application/json");
	req->setStatus(200);
	userver::Stream stream = req->send();
	v.serialize([&](char c){stream.putCharNB(c);});
	stream.flush() >> [req = std::move(req)](userver::Stream &s){
		//closure is necessary to keep pointers alive during async operation
		//destroy stream before the request
		s = userver::Stream();
	};
}

bool HttpAPI::get_api_login(Req &req, const Args &args) {
	userver::HeaderValue auth = req->get("Authorization");
	if (!auth.defined) {
		AuthService::basic_auth(*req);
		return true;
	}
	Auth::User usr = core->get_auth().lock()->get_user(*req);
	if (!usr.exists) {
		AuthService::basic_auth(*req);
		return true;
	}
	userver::HeaderValue redir = args["redir"];
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

	userver::HeaderValue auth = req->get("Authorization");
	userver::HeaderValue redir = args[redir];
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
		} catch (json::ParseError &e) {
			req->sendErrorPage(400, e.what());
		}
	};
	return true;
}

bool HttpAPI::delete_api_user(Req &req, const Args &args) {
	return get_api_logout(req, args);
}

bool HttpAPI::post_set_cookie(Req &req, const Args &args) {
	req->readBody(req,max_upload)
		>> [me = PHttpAPI(this)](Req &req, const std::string_view &body) {
			userver::QueryParser qp;
			qp.parse(body, false);
			userver::HeaderValue auth = qp["auth"];
			if (auth.defined) {
				std::string cookie = "auth=";
				cookie.append(auth);
				req->set("Set-Cookie", cookie);
			} else {
				req->sendErrorPage(400);
				return;
			}
			userver::HeaderValue redir = qp["redir"];
			if (redir.defined) {
				req->set("Location", redir);
				req->setStatus(302);
			} else {
				req->setStatus(202);
			}
			req->send("");
		};
	return true;
}
