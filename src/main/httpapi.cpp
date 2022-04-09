/*
 * httpapi.cpp
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#include <imtjson/serializer.h>
#include "httpapi.h"
#include "ssestream.h"

void HttpAPI::init(userver::OpenAPIServer &server) {
	PHttpAPI me;
	server.addPath("/").GET("General","WWW pages")
			.method(me,&HttpAPI::get_root);
	server.addPath("/set_cookie").POST("General","Set cookie", "Content is application/x-www-form-urlencoded, and expects auth=<content>. It directly copies this to the cookie (as session) - this is intended to be used by platform to allow direct entering to the administration using JWT token" )
			.method(me,&HttpAPI::post_set_cookie);
	server.addPath("/api/_up").GET("Monitoring","Returns 200 if server is running")
			.method(me,&HttpAPI::get_api__up);
	server.addPath("/api/data").GET("Statistics","Statistics datastream SSE")
			.method(me,&HttpAPI::get_api_data);
	server.addPath("/api/report.json").GET("Statistics","Statistics report snapshot")
			.method(me,&HttpAPI::get_api_report_json);
	server.addPath("/api/login").GET("General","Login using Basic Autentification","Requests for login dialog and after login is successful, redirects the user agent to the specified url",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_login);
	server.addPath("/api/logout").GET("General","Clears basic autentification state","It also deletes auth cookie to ensure that logout works",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_logout);
	server.addPath("/api/user").GET("General","Get information about current user","",)
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

bool HttpAPI::get_root(Req &req, const Args &args) {
	return static_pages(req, args.getPath());
}

void HttpAPI::send_json(Req &req, const json::Value &v) {
	req->setContentType("application/json");
	req->setStatus(200);
	userver::Stream stream = req->send();
	v.serialize([&](char c){stream.putCharNB(c);});
	stream.flush() >> [req = std::move(req)](userver::Stream &){
		//closure is necessary to keep pointers alive during async operation
		/*empty - we don't need to know, whether this failed */
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
	req->set("Set-Cookie","auth=;Max-Age=0");
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
