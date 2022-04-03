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
#include <imtjson/object.h>
#include <shared/logOutput.h>

using ondra_shared::logDebug;


Proxy::Proxy(const std::string &apiUrl, const std::string &timeUri)
:httpc(apiUrl)

{
}


std::uint64_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(httpc.now().time_since_epoch()).count();
}



json::Value Proxy::public_request(std::string method, json::Value data) {
	return httpc.GETq(method, data);
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

json::Value Proxy::private_request(Method method, const std::string &command, json::Value data) {
	if (!hasKey())
		throw std::runtime_error("Function requires valid API keys");

	auto n = now();
	data = data.replace("timestamp", n);

	std::ostringstream urlbuilder;
	urlbuilder << command;

	std::string request;
	HTTPJson::buildQuery(data, request);
	std::string sign = signData(privKey.str(),request);
	std::string url = urlbuilder.str();
	request.append("&signature=").append(sign);

	std::ostringstream response;

	json::Value res;

	json::Object headers;
	headers.set("X-MBX-APIKEY",pubKey);

	if (method == GET) {
		url = url + "?" + request;
		res = httpc.GET(url, headers);
	} else if (method == DELETE) {
		url = url + "?" + request;
		res = httpc.DELETE(url,json::String(), headers);
	} else {
		headers.set("Content-Type","application/x-www-form-urlencoded");
		if (method == POST) {
			res = httpc.POST(url, request, headers);
		} else {
			res = httpc.PUT(url, request, headers);
		}
	}
	return res;
}

bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty();
}
