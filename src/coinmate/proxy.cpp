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
	apiUrl = "https://coinmate.io/api/";
	auto now = std::chrono::system_clock::now();
	std::size_t init_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - start_time;
	nonce = init_time * 100;
}

bool Proxy::hasKey() const {
return  !privKey.empty()
			&& !pubKey.empty()
			&& !clientid.empty();

}

std::pair<std::string, std::uint64_t> Proxy::createSignature() {
	std::ostringstream msgbuff;
	msgbuff<<nonce<<clientid<<pubKey;
	std::string msg = msgbuff.str();
	unsigned char dbuff[100];
	unsigned int dbuff_size = sizeof(dbuff);
	HMAC(EVP_sha256(), privKey.data(), privKey.size(),
			reinterpret_cast<const unsigned char *>(msg.data()),
			msg.size(), dbuff, &dbuff_size);
	std::ostringstream digest;
	for (unsigned int i = 0; i < dbuff_size; i++) {
		digest << std::hex << std::setw(2) << std::setfill('0')
				<< std::uppercase << static_cast<unsigned int>(dbuff[i]);
	}
	return std::pair<std::string, std::uint64_t>(digest.str(),nonce++);
}

json::Value Proxy::request(Method method, std::string path, json::Value data) {

	std::string d = createQuery(data);
	std::ostringstream response;
	std::string url = apiUrl+path;
	std::istringstream src(d);

/*	auto reader = [&](char *buffer, size_t size, size_t nitems) {
		src.read(buffer,size*nitems);
		std::size_t cnt = src.gcount();
		return cnt;
	};*/

	if (!hasKey() && method != GET)
		throw std::runtime_error("This operation requires valid API key");

	const char *m = "";
	curl_handle.reset();
	switch(method) {
	case POST:
		m="POST";
		curl_handle.setOpt(cURLpp::Options::Post(true));
		curl_handle.setOpt(new cURLpp::Options::ReadStream(&src));
		curl_handle.setOpt(cURLpp::Options::PostFieldSize(d.length()));
		break;
	case PUT:
		m="PUT";
		curl_handle.setOpt(cURLpp::Options::Upload(true));
		curl_handle.setOpt(cURLpp::Options::ReadStream(&src));
		curl_handle.setOpt(cURLpp::Options::InfileSize(d.length()));
		break;
	case GET:
		m="GET";
		url = url + "?" + d;
		d.clear();
		break;
	}

	if (debug) {
		std::cerr << "Send: " << m << " " << url << " " << d << std::endl;
	}

	logDebug("Request: $1 $2 $3", m, url, d);


	curl_handle.setOpt(cURLpp::Options::Url(url));
	curl_handle.setOpt(cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	if (debug) {
		std::cerr << "Recv: " << response.str() << std::endl;
	}


	json::Value v = json::Value::fromString(response.str());
	if (v["error"].getBool()) throw std::runtime_error(v["errorMessage"].toString().c_str());
	return v["data"];

}

std::string Proxy::createQuery(json::Value data) {
	std::ostringstream out;
	if (hasKey()) {
		auto sig = createSignature();
		out << "clientId=" << clientid
			<< "&publicKey=" << pubKey
			<< "&nonce=" << sig.second
			<< "&signature=" << sig.first;
	}

	for(json::Value x: data) {
		out << "&" << x.getKey() << "=";
		json::String s = x.toString();
		if (!s.empty()) {
			char *esc = curl_easy_escape(curl_handle.getHandle(),s.c_str(), s.length());
			out << esc;
			curl_free(esc);
		}
	}
	return out.str();

}
