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
#include <random>

#include <imtjson/string.h>
#include <imtjson/object.h>
#include <imtjson/value.h>
#include "../shared/logOutput.h"

using json::Value;
using ondra_shared::logDebug;

Proxy::Proxy() {
	setTestnet(false);
}


bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty() ;
}


void Proxy::urlEncode(const std::string_view &text, std::ostream &out) {
	char* esc = curl_easy_escape(curl_handle.getHandle(), text.data(),
			text.length());
	out << esc;
	curl_free(esc);
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

	curl_handle.reset();
	std::string fpath = buildPath(path, query);
	std::string fdata = data.hasValue()?data.stringify().str():json::StrViewA();

	std::list<std::string> headers;

	if (hasKey()) {
		auto authdata = signRequest(verb, fpath, fdata);

		headers.push_back("api-expires: "+authdata.expires);
		headers.push_back("api-key: "+authdata.key);
		headers.push_back("api-signature: "+authdata.signature);

	}

	if (verb != "GET" && !fdata.empty()) {
		headers.push_back("Content-Type: application/json");
		headers.push_back("Accepts: application/json");
	}

	std::istringstream request(fdata);
	std::ostringstream response;

	std::string url = apiUrl + fpath;

	curl_handle.reset();
	if (verb == "POST") {
		curl_handle.setOpt(new cURLpp::Options::Post(true));
		curl_handle.setOpt(new cURLpp::Options::ReadStream(&request));
		curl_handle.setOpt(new cURLpp::Options::PostFieldSize(fdata.length()));
	} else if (verb == "PUT") {
		curl_handle.setOpt(new cURLpp::Options::Put(true));
		curl_handle.setOpt(new cURLpp::Options::ReadStream(&request));
		curl_handle.setOpt(new cURLpp::Options::PostFieldSize(fdata.length()));
	} else if (verb == "DELETE") {
		curl_handle.setOpt(new cURLpp::Options::CustomRequest("DELETE"));
	}

	curl_handle.setOpt(new cURLpp::Options::HttpHeader(headers));

	curl_handle.setOpt(new cURLpp::Options::Url(url));
	curl_handle.setOpt(new cURLpp::Options::WriteStream(&response));

	if (debug) {
		std::cerr << "SEND: " << verb <<  " " << url << std::endl;
		if (!fdata.empty()) std::cerr << "SEND BODY: " << fdata << std::endl;
	}

	curl_handle.perform();

	if (debug) {
		std::cerr << "RECV: " << response.str() << std::endl;
	}



	auto resp_code = curlpp::infos::ResponseCode::get(curl_handle);
	if (resp_code /100 != 2) {
		std::string errmsg;
		try {
			Value p = Value::fromString(response.str());
			errmsg = std::to_string(resp_code).append(" ")
					.append(std::string_view(p["error"]["message"].getString()))
					.append(" ")
					.append(std::string_view(p["error"]["ordRejReason"].getString()));
		} catch (...) {
			errmsg = std::to_string(resp_code).append(" ").append(response.str());
		}
		throw std::runtime_error(errmsg);
	} else {
		try {
			Value p = Value::fromString(response.str());
			return p;
		} catch (...) {
			std::string errmsg;
			errmsg = std::to_string(500).append(" ").append(response.str());
			throw std::runtime_error(errmsg);
		}

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
