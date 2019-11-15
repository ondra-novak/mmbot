/*
 * httpjson.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include <imtjson/string.h>
#include <imtjson/parser.h>
#include "httpjson.h"

HTTPJson::HTTPJson(simpleServer::HttpClient &httpc,
		const std::string_view &baseUrl)
:httpc(httpc),baseUrl(baseUrl)
{
}

void HTTPJson::setToken(const std::string_view &token) {
	this->token = token;
}

static simpleServer::SendHeaders hdrs(const std::string &token, bool body) {

	simpleServer::SendHeaders hdr;
	if (body) hdr.contentType("application/json");
	if (!token.empty()) hdr("Authorization","Bearer "+token);
	return hdr;
}

json::Value HTTPJson::GET(const std::string_view &path, unsigned int expectedCode) {
	std::string url = baseUrl;
	url.append(path);



	auto resp = httpc.request("GET", url, hdrs(token,false));
	unsigned int st = resp.getStatus();
	if (st != expectedCode) {
		throw simpleServer::HTTPStatusException(st, resp.getMessage());
	}
	return json::Value::parse(resp.getBody());
}

json::Value HTTPJson::SEND(const std::string_view &path,
		const std::string_view &method, const json::Value &data,
		unsigned int expectedCode) {

	std::string url = baseUrl;
	url.append(path);
	auto sdata = data.stringify();

	auto resp = httpc.request(method, url, hdrs(token,true), sdata.str());
	unsigned int st = resp.getStatus();
	if (st != expectedCode) {
		throw simpleServer::HTTPStatusException(st, resp.getMessage());
	}
	return json::Value::parse(resp.getBody());

}

json::Value HTTPJson::POST(const std::string_view &path,
		const json::Value &data, unsigned int expectedCode) {
	return SEND(path, "POST", data, expectedCode);
}

json::Value HTTPJson::PUT(const std::string_view &path, const json::Value &data,
		unsigned int expectedCode) {
	return SEND(path, "PUT", data, expectedCode);
}

json::Value HTTPJson::DELETE(const std::string_view &path,
		const json::Value &data, unsigned int expectedCode) {
	return SEND(path, "DELETE", data, expectedCode);
}
