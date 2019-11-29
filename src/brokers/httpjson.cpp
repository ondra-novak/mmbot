/*
 * httpjson.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include <imtjson/string.h>
#include <imtjson/parser.h>
#include "httpjson.h"

#include <simpleServer/urlencode.h>
#include "../shared/logOutput.h"
#include "log.h"

using ondra_shared::logDebug;
using simpleServer::urlEncode;

HTTPJson::HTTPJson(simpleServer::HttpClient &&httpc,
		const std::string_view &baseUrl)
:httpc(std::move(httpc)),baseUrl(baseUrl)
{
	httpc.setConnectTimeout(5000);
	httpc.setIOTimeout(10000);
}


enum class BodyType {
	none,
	form,
	json
};

static simpleServer::SendHeaders hdrs(const json::Value &headers) {

	simpleServer::SendHeaders hdr;
	for (json::Value v: headers) {
		auto k = v.getKey();
		if (k != "Connection") {
			hdr(k, v.toString());
		}

	}
	hdr("Connection","close");
	if (!headers["Accept"].defined()) hdr("Accept","application/json");
	return hdr;
}

json::Value HTTPJson::GET(const std::string_view &path, json::Value &&headers, unsigned int expectedCode) {
	std::string url = baseUrl;
	url.append(path);

	logDebug("GET $1", url);

	auto resp = httpc.request("GET", url, hdrs(headers));
	unsigned int st = resp.getStatus();
	if ((expectedCode && st != expectedCode) || (!expectedCode && st/100 != 2)) {
		throw UnknownStatusException(st, resp.getMessage(),resp);
	}
	json::Value r = json::Value::parse(resp.getBody());
	logDebug("RECV: $1", r);
	return r;
}

json::Value HTTPJson::SEND(const std::string_view &path,
		const std::string_view &method, const json::Value &data,
		json::Value &&headers,
		unsigned int expectedCode) {

	std::string url = baseUrl;
	url.append(path);
	auto sdata = data.toString();

	if (!headers["Content-Type"].defined()) {
		if (data.type() != json::string ) {
			headers = headers.replace("Content-Type", "application/json");
		} else {
			headers = headers.replace("Content-Type", "application/x-www-form-urlencoded");
		}
	}

	logDebug("$1 $2 - data $3", method, url, data);


	auto resp = httpc.request(method, url, hdrs(headers), sdata.str());
	unsigned int st = resp.getStatus();
	if ((expectedCode && st != expectedCode) || (!expectedCode && st/100 != 2)) {
		throw UnknownStatusException(st, resp.getMessage(), resp);
	}
	json::Value r = json::Value::parse(resp.getBody());
	logDebug("RECV: $1", r);
	return r;

}

json::Value HTTPJson::POST(const std::string_view &path,
		const json::Value &data, json::Value &&headers, unsigned int expectedCode) {
	return SEND(path, "POST", data, std::move(headers), expectedCode);
}

json::Value HTTPJson::PUT(const std::string_view &path, const json::Value &data,
		json::Value &&headers, unsigned int expectedCode) {
	return SEND(path, "PUT", data, std::move(headers), expectedCode);
}

json::Value HTTPJson::DELETE(const std::string_view &path,
		const json::Value &data,json::Value &&headers,  unsigned int expectedCode) {
	return SEND(path, "DELETE", data, std::move(headers), expectedCode);
}
