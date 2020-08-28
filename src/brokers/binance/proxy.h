/*
 * proxy.h
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_COINMATE_PROXY_H_
#define SRC_COINMATE_PROXY_H_

#include <cstdint>
#include <imtjson/value.h>
#include <imtjson/string.h>
#include "../httpjson.h"

class Proxy {
public:

	Proxy(const std::string &apiUrl, const std::string &timeUri);

	std::string apiUrl;
	json::String privKey;
	json::String pubKey;

	HTTPJson httpc;

	enum Method {
		GET,
		POST,
		PUT,
		DELETE
	};

	json::Value public_request(std::string method, json::Value data);
	json::Value private_request(Method method, std::string command, json::Value data);

	bool hasKey() const;
	void setTime(std::uint64_t t);
	std::uint64_t now();

	bool debug = false;

	std::string timeUri;


private:
	std::int64_t time_diff = 0;
	std::uint64_t time_sync = 0;
	void buildParams(const json::Value& params, std::ostream& data);
};



#endif /* SRC_COINMATE_PROXY_H_ */
