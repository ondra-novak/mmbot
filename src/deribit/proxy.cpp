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
#include "../imtjson/src/imtjson/object.h"
#include "../shared/logOutput.h"

using ondra_shared::logDebug;

Proxy::Proxy(Config config):config(config) {
	hasKey = !config.privKey.empty() && !config.pubKey.empty();
}

void Proxy::setTimeDiff(std::intptr_t t) {
	this->time_diff = t;
}

json::Value Proxy::request(std::string_view method, json::Value params, bool auth) {

	const std::string *tk(auth?&getAccessToken():nullptr);
	if (tk) params = params.replace("access_token",*tk);

	json::Value req (json::object, {
			json::Value("method",method),
			json::Value("params",params),
			json::Value("id",req_id++),
			json::Value("jsonrpc","2.0"),
	});
	std::string req_str = req.stringify().str();
	std::istringstream req_strm(req_str);
	std::ostringstream response;

	if (debug) {
			std::cerr << "SEND: " << req_str << std::endl;
	}

	curl_handle.reset();
	curl_handle.setOpt(new cURLpp::Options::Post(true));
	curl_handle.setOpt(new cURLpp::Options::ReadStream(&req_strm));
	curl_handle.setOpt(new cURLpp::Options::PostFieldSize(req_str.length()));

	if (tk) {
		std::string hdr = "Authorization: bearer "+ *tk;
		std::list<std::string> headers;
		headers.push_back(hdr);
		curl_handle.setOpt(new cURLpp::Options::HttpHeader(headers));
	}

	curl_handle.setOpt(new cURLpp::Options::Url(config.apiUrl));
	curl_handle.setOpt(new cURLpp::Options::WriteStream(&response));
	curl_handle.perform();

	std::string rsp = response.str();
	auto resp_code = curlpp::infos::ResponseCode::get(curl_handle);

	if (debug) {
		std::cerr << "Status: " << resp_code << std::endl;
		std::cerr << "RECV: " << rsp << std::endl;
	}

	if (resp_code/100 != 2) throw std::runtime_error(rsp);


	json::Value resp =  json::Value::fromString(rsp);
	json::Value success = resp["result"];
	json::Value error = resp["error"];
	if (error.defined()) throw std::runtime_error(error.toString().str());
	else return success;

}
/*
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
*/

const std::string &Proxy::getAccessToken() {
	if (!auth_token.empty() && std::chrono::system_clock::now() < auth_token_expire) {
		return auth_token;
	}
	/*
	std::random_device rdev;
	std::uniform_int_distribution rnd(0,35);
	std::string nonce;
	nonce.reserve(8);
	for (int i = 0; i < 8; i++) {
		char c = rnd(rdev);
		if (c > 9) c+='a'-10;else c+='0';
		nonce.push_back(c);
	}*/
//	std::ostringstream payload;

//	std::size_t time = request("public/get_time",json::Object(),false).getUInt();

/*	payload << time << "\n"
			<< nonce << "\n\n";
*/
//	std::string signature = signData(config.privKey, payload.str());


	auto resp = request("public/auth",json::Object
			("grant_type","client_credentials") /* Don't know, but client_signature doesn't work for me*/
			("client_id",config.pubKey)
			("client_secret",config.privKey)
			("scope",config.scopes)
/*			("timestamp",time)
			("signature",signature)
			("nonce",nonce)*/, false);

	std::size_t expires =resp["expires_in"].getUInt();
	auth_token = resp["access_token"].getString();
	auth_token_expire = std::chrono::system_clock::now() + std::chrono::seconds(expires-10);
	return auth_token;
}

std::uintptr_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();

}



