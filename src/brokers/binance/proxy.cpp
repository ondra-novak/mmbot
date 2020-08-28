/*
 * proxy.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */




#include "proxy.h"

#include <openssl/hmac.h>
#include <iomanip>

#include <sstream>
#include <chrono>

#include <imtjson/string.h>
#include <imtjson/parser.h>
#include <simpleServer/http_client.h>
#include <simpleServer/urlencode.h>
#include <imtjson/object.h>
#include <shared/logOutput.h>

using ondra_shared::logDebug;
using namespace simpleServer;


Proxy::Proxy(const std::string &apiUrl, const std::string &timeUri)
:httpc(HttpClient("+https://mmbot.trade",
		newHttpsProvider(),
		newNoProxyProvider()), apiUrl)
,timeUri(timeUri)

{
}


std::uint64_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::steady_clock::now().time_since_epoch()
						 ).count() + this->time_diff;

}

void Proxy::setTime(std::uint64_t t ) {
	this->time_diff = 0;
	auto n = now();
	this->time_diff = t - n;
}

void Proxy::buildParams(const json::Value& params, std::ostream& data) {
	for (json::Value field : params) {
		data << "&" << field.getKey() << "=";
		json::String s = field.toString();
		if (!s.empty()) {
			data << simpleServer::urlEncode(s);
		}
	}
}

json::Value Proxy::public_request(std::string method, json::Value data) {
	std::ostringstream urlbuilder;
	urlbuilder << apiUrl <<  method;
	buildParams(data, urlbuilder);
	std::ostringstream response;

	return httpc.GET(urlbuilder.str());

}

static std::string signData(std::string_view key, std::string_view data) {
	unsigned char dbuff[100];
	unsigned int dbuff_size = sizeof(dbuff);
	HMAC(EVP_sha256(), key.data(), key.size(),
			reinterpret_cast<const unsigned char *>(data.data()),
			data.size(), dbuff, &dbuff_size);
	std::ostringstream digest;
	for (unsigned int i = 0; i < dbuff_size; i++) {
		digest << std::hex << std::setw(2) << std::setfill('0')
				 << static_cast<unsigned int>(dbuff[i]);
	}
	return digest.str();
}

json::Value Proxy::private_request(Method method, std::string command, json::Value data) {
	if (!hasKey())
		throw std::runtime_error("Function requires valid API keys");

	auto n = now();
	if (n > time_sync) {
		json::Value tdata = public_request(timeUri,json::Value());
		auto m = tdata["serverTime"].getUIntLong();
		setTime(m);
		n = now();
		time_sync = n + (3600*1000); //- one hour
		logDebug("Time sync: $1. Next sync at: $2", n, time_sync);

	}
	data = data.replace("timestamp", n);


	std::ostringstream urlbuilder;
	urlbuilder << command;

	std::ostringstream databld;
	buildParams(data, databld);

	std::string request = databld.str().substr(1);
	std::string sign = signData(privKey.str(),request);
	std::string url = urlbuilder.str();
	request.append("&signature=").append(sign);



	std::ostringstream response;

	json::Value res;
	//json::Object hdrs("Content-Type","application/x-www-form-urlencoded");

	json::Object headers;
	headers("X-MBX-APIKEY",pubKey);

	try {
		if (method == GET) {
			url = url + "?" + request;
			res = httpc.GET(url, headers);
		} else if (method == DELETE) {
			url = url + "?" + request;
			res = httpc.DELETE(url,json::String(), headers);
		} else {
			headers("Content-Type","application/x-www-form-urlencoded");
			if (method == POST) {
				res = httpc.POST(url, request, headers);
			} else {
				res = httpc.PUT(url, request, headers);
			}
		}
	} catch (const HTTPJson::UnknownStatusException &e) {
		json::Value err;
		try {err = json::Value::parse(e.response.getBody());} catch (...) {}
		if (!err.hasValue()) throw;
		if (err["code"].defined()) {
			json::String msg({err["code"].toString()," ",err["msg"].toString()});
			throw std::runtime_error(msg.c_str());
		}
		else {
			throw std::runtime_error(err.toString().c_str());
		}
	}
	return res;
}

bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty();
}
