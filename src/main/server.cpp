/*
 * server.cpp
 *
 *  Created on: 4. 11. 2021
 *      Author: ondra
 */

#include "server.h"

#include <imtjson/object.h>
#include <userver/http_server.h>
#include <imtjson/value.h>
#include <imtjson/string.h>
using namespace userver;

void Server::log(ReqEvent event, const HttpServerRequest &req) noexcept {
	if (event == ReqEvent::done) {
		std::lock_guard _(mx);
		auto now = std::chrono::system_clock::now();
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(now-req.getRecvTime());
		char buff[100];
		snprintf(buff,100,"%1.3f ms", dur.count()*0.001);
		lo.progress("#$1 $2 $3 $4 $5 $6", req.getIdent(), req.getStatus(), req.getMethod(), req.getHost(), req.getPath(), buff);
	}
}
void Server::log(const HttpServerRequest &req, const std::string_view &msg) noexcept  {
	std::lock_guard _(mx);
	lo.note("#$1 $2", req.getIdent(), msg);
}

Server::Server():lo("http") {}

void Server::unhandled() noexcept  {
	try {
		throw;
	} catch (std::exception &e) {
		std::lock_guard _(mx);
		lo.error("Unhandled exception: $1", e.what());
	} catch (...) {

	}
}

void Server::error_page(HttpServerRequest &r, int status, const std::string_view &desc) noexcept {
	if (r.getPath().substr(r.getRootPath().length()).compare(0,5,"/api/") == 0) {
		if (desc.empty()) {
			error_page(r,status,userver::getStatusCodeMsg(status));
		} else {
			r.setStatus(status);
			json::Value err = json::Object {{"error",json::Object{
				{"code", status},
				{"message",desc }}}
			};
			r.setContentType("application/json");
			auto str = err.stringify();
			r.send(str.str());
		}
	} else {
		userver::OpenAPIServer::error_page(r, status, desc);
	}



}



