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
#include <simpleServer/http_client.h>
#include <simpleServer/urlencode.h>
#include <imtjson/object.h>
#include "../shared/logOutput.h"

using ondra_shared::logDebug;
using namespace simpleServer;

static constexpr std::uint64_t start_time = 1557858896532;
Proxy::Proxy()
	:httpc(HttpClient("MMBot coinmate broker", newHttpsProvider(), newNoProxyProvider()), "https://coinmate.io/api/")
	{
	auto now = std::chrono::system_clock::now();
	std::uint64_t init_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - start_time;
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

	if (!hasKey() && method != GET)
		throw std::runtime_error("This operation requires valid API key");
	static json::Object ctx("Content-Type","application/x-www-form-urlencoded");

	json::Value v;
	switch(method) {
	case POST:
		v = httpc.POST(path , createQuery(data), ctx);
		break;
	case PUT:
		v = httpc.PUT(path, createQuery(data), ctx);
		break;
	case GET:
		v = httpc.GET(path + "?" + createQuery(data));
	}

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
			out << urlEncode(s.str());
		}
	}
	return out.str();

}
