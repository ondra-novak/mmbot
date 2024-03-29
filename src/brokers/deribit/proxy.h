/*
 * proxy.h
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_DERIBIT_PROXY_H_
#define SRC_DERIBIT_PROXY_H_
#include <chrono>

#include <imtjson/value.h>
#include "../httpjson.h"

class Proxy {
public:

	Proxy();

	std::string apiUrl;
	std::string privKey;
	std::string pubKey;
	std::string scopes;

	HTTPJson httpc;

	bool testnet;

	///Send request
	/**
	 * @param method method
	 * @param params params (must be object)
	 * @param auth set true to include access token. set false for public request
	 * @return success response
	 * @exception std::runtime_error error response
	 */
	json::Value request(std::string_view method, json::Value params, bool auth);
	const std::string &getAccessToken();

	bool hasKey() const;
	bool debug = false;

	static std::uint64_t now();

	void setTestnet(bool testnet);


private:
	std::string auth_token;
	std::chrono::system_clock::time_point auth_token_expire;
	std::size_t req_id = 1;

};



#endif /* SRC_DERIBIT_PROXY_H_ */
