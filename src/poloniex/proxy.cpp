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
#include <curlpp/Options.hpp>
#include <chrono>

#include <imtjson/string.h>
#include "../shared/logOutput.h"

using ondra_shared::logDebug;

static constexpr std::uint64_t start_time = 1557858896532;
Proxy::Proxy() {

	apiPrivUrl="https://poloniex.com/tradingApi";
	apiPublicUrl="https://poloniex.com/public";

	auto now = std::chrono::system_clock::now();
	std::size_t init_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - start_time;
	nonce = init_time * 100;
}

bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty();
}


json::Value Proxy::getTicker() {

	return public_request("returnTicker", json::Value());
}

void Proxy::buildParams(const json::Value& params, std::ostream& data) {
	for (json::Value field : params) {
		data << "&" << field.getKey() << "=";
		json::String s = field.toString();
		if (!s.empty()) {
			char* esc = curl_easy_escape(curl_handle.getHandle(), s.c_str(),
					s.length());
			data << esc;
			curl_free(esc);
		}
	}
}

json::Value Proxy::public_request(std::string method, json::Value data) {
	std::ostringstream urlbuilder;
	urlbuilder << apiPublicUrl << "?command=" << method;
	buildParams(data, urlbuilder);
	std::ostringstream response;

	if (debug) {
		std::cerr << "Send: " << urlbuilder.str() << std::endl;
	}


	curl_handle.reset();


	curl_handle.setOpt(cURLpp::Options::Url(urlbuilder.str()));
	curl_handle.setOpt(cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	if (debug) {
		std::cerr << "Recv: " << response.str() << std::endl;
	}


	return json::Value::fromString(response.str());

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

json::Value Proxy::private_request(std::string method, json::Value data) {
	if (!hasKey())
		throw std::runtime_error("Function requires valid API keys");

	std::ostringstream databld;
	buildParams({json::Value("command", method), json::Value("nonce", ++nonce)}, databld);
	buildParams(data, databld);

	std::string request = databld.str().substr(1);
	std::ostringstream response;
	std::istringstream src(request);


	if (debug) {
		std::cerr << "Send: " << request << std::endl;
	}


	curl_handle.reset();

	curl_handle.setOpt(new cURLpp::Options::Post(true));
	curl_handle.setOpt(new cURLpp::Options::ReadStream(&src));
	curl_handle.setOpt(new cURLpp::Options::PostFieldSize(request.length()));

	std::list<std::string> headers;
	headers.push_back("Key: "+pubKey);
	headers.push_back("Sign: "+signData(privKey, request));;

	curl_handle.setOpt(new cURLpp::Options::HttpHeader(headers));

	curl_handle.setOpt(new cURLpp::Options::Url(apiPrivUrl));
	curl_handle.setOpt(new cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	if (debug) {
		std::cerr << "Recv: " << response.str() << std::endl;
	}

	json::Value v =  json::Value::fromString(response.str());
	if (v["error"].defined()) throw std::runtime_error(v["error"].toString().c_str());
	return v;
}


