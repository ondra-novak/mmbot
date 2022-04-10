/*
 * server.h
 *
 *  Created on: 4. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SERVER_H_
#define SRC_MAIN_SERVER_H_

#include <shared/logOutput.h>
#include <userver/openapi.h>
#include <mutex>


class Server: public userver::OpenAPIServer {
public:
	Server();

	virtual void log(userver::ReqEvent event, const userver::HttpServerRequest &req) override;
	virtual void log(const userver::HttpServerRequest &req, const std::string_view &msg) override;
	virtual void unhandled() override;
protected:
	std::mutex mx;
	ondra_shared::LogObject lo;

};

using PServer = std::shared_ptr<Server>;



#endif /* SRC_MAIN_SERVER_H_ */
