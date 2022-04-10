/*
 * server.cpp
 *
 *  Created on: 4. 11. 2021
 *      Author: ondra
 */

#include "server.h"

using namespace userver;

void Server::log(ReqEvent event, const HttpServerRequest &req) {
	if (event == ReqEvent::done) {
		std::lock_guard _(mx);
		auto now = std::chrono::system_clock::now();
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(now-req.getRecvTime());
		char buff[100];
		snprintf(buff,100,"%1.3f ms", dur.count()*0.001);
		lo.progress("#$1 $2 $3 $4 $5 $6", req.getIdent(), req.getStatus(), req.getMethod(), req.getHost(), req.getPath(), buff);
	}
}
void Server::log(const HttpServerRequest &req, const std::string_view &msg) {
	std::lock_guard _(mx);
	lo.note("#$1 $2", req.getIdent(), msg);
}

Server::Server():lo("http") {}

void Server::unhandled() {
	try {
		throw;
	} catch (std::exception &e) {
		std::lock_guard _(mx);
		lo.error("Unhandled exception: $1", e.what());
	} catch (...) {

	}
}





