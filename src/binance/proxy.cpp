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
#include <curlpp/Infos.hpp>
#include <chrono>

#include <imtjson/string.h>
#include "../shared/logOutput.h"

using ondra_shared::logDebug;

static constexpr std::uint64_t start_time = 1557858896532;
Proxy::Proxy(Config config):config(config) {
	auto now = std::chrono::system_clock::now();
	std::size_t init_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - start_time;
	nonce = init_time * 100;
	hasKey = !config.privKey.empty() && !config.pubKey.empty();

}

void Proxy::setTimeDiff(std::intptr_t t) {
	this->time_diff = t;
}

std::uintptr_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();

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
	urlbuilder << config.apiUrl <<  method;
	buildParams(data, urlbuilder);
	std::ostringstream response;
	curl_handle.reset();

	if (debug) {
			std::cerr << "SEND: " << urlbuilder.str() << std::endl;
	}


	curl_handle.setOpt(cURLpp::Options::Url(urlbuilder.str()));
	curl_handle.setOpt(cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	if (debug) {
			std::cerr << "RECV: " << response.str() << std::endl;
	}


	return json::Value::fromString(response.str());

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
	if (!hasKey)
		throw std::runtime_error("Function requires valid API keys");

	data = data.replace("timestamp", now()+time_diff);

	std::ostringstream urlbuilder;
	urlbuilder << config.apiUrl <<  command;

	std::ostringstream databld;
	buildParams(data, databld);

	std::string request = databld.str().substr(1);
	std::string sign = signData(config.privKey,request);
	std::string url = urlbuilder.str();
	request.append("&signature=").append(sign);



	std::ostringstream response;
	std::istringstream src(request);
	curl_handle.reset();

	if (method == GET) {
		url = url + "?" + request;
		if (debug) std::cerr << "SEND: GET " << url << std::endl;
	} else if (method == DELETE) {
		url = url + "?" + request;
		if (debug) std::cerr << "SEND: DELETE " << url << std::endl;
		curl_handle.setOpt(new cURLpp::Options::CustomRequest("DELETE"));
	} else {
		if (method == POST) {
			if (debug) std::cerr << "SEND: POST " << url << "( " << request << " )" << std::endl;
			curl_handle.setOpt(new cURLpp::Options::Post(true));
		} else {
			if (debug) std::cerr << "SEND: PUT " << url << "( " << request << " )" << std::endl;
			curl_handle.setOpt(new cURLpp::Options::Put(true));
		}
		curl_handle.setOpt(new cURLpp::Options::ReadStream(&src));
		curl_handle.setOpt(new cURLpp::Options::PostFieldSize(request.length()));
	}

	std::list<std::string> headers;
	headers.push_back("X-MBX-APIKEY: "+config.pubKey);

	curl_handle.setOpt(new cURLpp::Options::HttpHeader(headers));

	curl_handle.setOpt(new cURLpp::Options::Url(url));
	curl_handle.setOpt(new cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	std::string rsp = response.str();
	auto resp_code = curlpp::infos::ResponseCode::get(curl_handle);

	if (debug) std::cerr << "RECV: " << resp_code << " " << rsp << std::endl;

	//	std::cerr << rsp << std::endl;

	if (resp_code/100 != 2) throw std::runtime_error(rsp);

	json::Value v =  json::Value::fromString(rsp);
	return v;
}


