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
#include <random>

#include <imtjson/string.h>
#include <imtjson/object.h>
#include <imtjson/value.h>
#include <simpleServer/http_client.h>
#include <simpleServer/urlencode.h>
#include <imtjson/parser.h>
#include <shared/logOutput.h>

using json::Value;
using ondra_shared::logDebug;
using namespace simpleServer;

Proxy::Proxy()
:httpc(HttpClient("MMBot Bitmex broker",
		newHttpsProvider(),
		newNoProxyProvider()), "")
 {
	setTestnet(false);
}


bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty() ;
}


void Proxy::urlEncode(const std::string_view &text, std::ostream &out) {
	out << simpleServer::urlEncode(text);
}

std::string Proxy::buildPath(const std::string_view path, const json::Value &query) {
	std::ostringstream pathbuild;
	pathbuild << path;
	if (query.type() == json::object && !query.empty()) {
		char sep = '?';
		for (Value v : query) {
			pathbuild << sep;
			sep = '&';
			urlEncode(v.getKey(), pathbuild);
			pathbuild << '=';
			urlEncode(v.toString().str(), pathbuild);
		}
	}
	return pathbuild.str();
}

json::Value Proxy::request(
		const std::string_view &verb,
		const std::string_view path,
		const json::Value &query,
		const json::Value &data) {

	std::string fpath = buildPath(path, query);
	std::string fdata = data.hasValue()?data.stringify().str():json::StringView();

	json::Object headers;



	if (hasKey()) {
		auto authdata = signRequest(verb, fpath, fdata);

		headers.set("api-expires",authdata.expires);
		headers.set("api-key",authdata.key);
		headers.set("api-signature",authdata.signature);

	}

	if (verb != "GET" && !fdata.empty()) {
		headers.set("Content-Type","application/json");
		headers.set("Accepts","application/json");
	}


	std::string url = apiUrl + fpath;
	Value res;

	try {

		if (verb == "POST") {
			res = httpc.POST(url,data,headers);
		} else if (verb == "PUT") {
			res = httpc.PUT(url, data, headers);
		} else if (verb == "DELETE") {
			res = httpc.DELETE(url,data,headers);
		} else {
			res = httpc.GET(url, headers);
		}
		return res;
	} catch (HTTPJson::UnknownStatusException &e) {
		std::string errmsg;
		try {
			Value p = Value::parse(e.response.getBody());
			errmsg = std::to_string(e.getStatusCode()).append(" ")
					.append(std::string_view(p["error"]["message"].getString()))
					.append(" ")
					.append(std::string_view(p["error"]["ordRejReason"].getString()));
		} catch (...) {
			errmsg = std::to_string(e.getStatusCode()).append(" ").append(e.getStatusMessage());
		}
		throw std::runtime_error(errmsg);
	}
}


std::uint64_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::seconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();

}

void Proxy::setTestnet(bool testnet) {
	apiUrl = testnet?"https://testnet.bitmex.com":"https://www.bitmex.com";
	this->testnet = testnet;
}

Proxy::AuthData Proxy::signRequest(const std::string_view &verb,
		const std::string_view &path, const std::string_view &data) {

	std::string expires = std::to_string(now()+30);
	std::ostringstream buff;
	buff << verb << path << expires << data;
	std::string msg = buff.str();
	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);
	HMAC(EVP_sha256(),privKey.data(),privKey.length()
			, reinterpret_cast<unsigned char *>(msg.data()), msg.length(),
			digest, &digest_len);

	std::ostringstream digeststr;
	for (unsigned int i = 0; i < digest_len; i++) {
		digeststr << std::hex << std::setw(2) << std::setfill('0')
				 << static_cast<unsigned int>(digest[i]);
	}
	return AuthData {
		pubKey,
		digeststr.str(),
		expires
	};
}
