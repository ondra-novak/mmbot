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
#include <simpleServer/http_client.h>
#include <imtjson/parser.h>
#include <shared/logOutput.h>

using ondra_shared::logDebug;
using namespace simpleServer;

Proxy::Proxy()
:httpc(HttpClient("MMBot Deribit broker",
		newHttpsProvider(),
		newNoProxyProvider(), newCachedDNSProvider(60)), "")
 {
	setTestnet(false);
}


bool Proxy::hasKey() const {
	return !privKey.empty() && !pubKey.empty() && !scopes.empty();
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

	json::Object headers;
	headers.set("Content-Type","application/json");

	if (tk) {
		headers.set("Authorization","bearer "+ *tk);
	}

	json::Value resp;
	try {
		resp = httpc.POST(apiUrl,req,headers);
	} catch (const HTTPJson::UnknownStatusException &e) {
		try {
			resp = json::Value::parse(e.response.getBody());
		} catch (...) {
			std::string buff;
			auto stream = e.response.getBody();
			for (StrViewA x (stream.read());!x.empty();x = StrViewA(stream.read())) {
				buff.append(x.data, x.length);
			}
			resp = json::Object({{"error", json::Object({{"code",e.getStatusCode()},{"message", buff}})}});
		}
	}
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


	auto resp = request("public/auth",json::Object({
		{"grant_type","client_credentials"}, /* Don't know, but client_signature doesn't work for me*/
		{"client_id",pubKey},
		{"client_secret",privKey},
		{"scope",scopes},
/*			{"timestamp",time},
			{"signature",signature},
			{"nonce",nonce}*/}), false);

	std::uint64_t expires =resp["expires_in"].getUIntLong();
	auth_token = resp["access_token"].getString();
	auth_token_expire = std::chrono::system_clock::now() + std::chrono::seconds(expires-10);
	return auth_token;
}

std::uint64_t Proxy::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();

}

void Proxy::setTestnet(bool testnet) {
	apiUrl = testnet?"https://test.deribit.com/api/v2":"https://www.deribit.com/api/v2";
	this->testnet = testnet;
}
