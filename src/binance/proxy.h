/*
 * proxy.h
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_COINMATE_PROXY_H_
#define SRC_COINMATE_PROXY_H_
#include <curlpp/Easy.hpp>

#include <imtjson/value.h>
#include "config.h"

class Proxy {
public:

	Proxy(Config config);

	Config config;
	cURLpp::Easy curl_handle;

	std::uint64_t nonce;

	enum Method {
		GET,
		POST,
		PUT,
		DELETE
	};

	json::Value public_request(std::string method, json::Value data);
	json::Value private_request(Method method, std::string command, json::Value data);

	bool hasKey;
	void setTimeDiff(std::intptr_t t);
	static std::uintptr_t now();

private:
	std::intptr_t time_diff = 0;
	void buildParams(const json::Value& params, std::ostream& data);
};



#endif /* SRC_COINMATE_PROXY_H_ */
