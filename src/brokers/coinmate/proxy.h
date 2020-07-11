/*
 * proxy.h
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_COINMATE_PROXY_H_
#define SRC_COINMATE_PROXY_H_

#include <imtjson/value.h>
#include "../httpjson.h"

class Proxy {
public:

	Proxy();

	std::string apiUrl;
	std::string privKey;
	std::string pubKey;
	std::string clientid;

	HTTPJson httpc;

	std::uint64_t nonce;

	std::pair<std::string, std::uint64_t> createSignature();

	enum Method {
		GET, POST, PUT
	};

	bool hasKey() const;;
	bool debug = false;

	json::Value request(Method method, std::string path, json::Value data);
	std::string createQuery(json::Value data);

};



#endif /* SRC_COINMATE_PROXY_H_ */
