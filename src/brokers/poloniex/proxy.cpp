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
#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <shared/logOutput.h>

using ondra_shared::logDebug;
static constexpr std::uint64_t start_time = 1557858896532;
Proxy::Proxy():httpc("")
 {

	apiPrivUrl="https://poloniex.com/tradingApi";
	apiPublicUrl="https://poloniex.com/public";

	auto now = std::chrono::system_clock::now();
	auto init_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - start_time;
	nonce = init_time * 100;
}

bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty();
}


json::Value Proxy::getTicker() {

	return public_request("returnTicker", json::Value());
}

json::Value Proxy::public_request(std::string_view method, json::Value data) {
	auto l = apiPublicUrl.size();
	apiPublicUrl.append(method);
	auto r = httpc.GETq(apiPublicUrl, data);
	apiPublicUrl.resize(l);
	return r;
}

static std::string signData(std::string_view key, std::string_view data) {
	unsigned char dbuff[100];
	unsigned int dbuff_size = sizeof(dbuff);
	HMAC(EVP_sha512(), key.data(), key.size(),
			reinterpret_cast<const unsigned char *>(data.data()),
			data.size(), dbuff, &dbuff_size);
	std::ostringstream digest;
	for (unsigned int i = 0; i < dbuff_size; i++) {
		digest << std::hex << std::setw(2) << std::setfill('0')
				<< std::uppercase << static_cast<unsigned int>(dbuff[i]);
	}
	return digest.str();
}

json::Value Proxy::private_request(std::string_view method, json::Value data) {
	if (!hasKey())
		throw std::runtime_error("Function requires valid API keys");



	std::string request;
	HTTPJson::buildQuery({json::Value("command", method), json::Value("nonce", ++nonce)}, request);
	HTTPJson::buildQuery(data, request);
	std::ostringstream response;
	std::istringstream src(request);



	json::Object headers;
	headers.set("Accepts","application/json");
	headers.set("Content-Type","application/x-www-form-urlencoded");
	headers.set("Key",pubKey);
	headers.set("Sign",signData(privKey, request));;

	try {
		json::Value v = httpc.POST(apiPrivUrl, request, headers);
		if (v["error"].defined()) throw std::runtime_error(v["error"].toString().c_str());
		return v;
	} catch (const HTTPJson::UnknownStatusException &e) {
		json::Value err = e.body;
		if (!err.hasValue()) throw;
		if (err["error"].defined()) throw std::runtime_error(err["error"].toString().c_str());
		else throw std::runtime_error(err.toString().c_str());
	}
}


