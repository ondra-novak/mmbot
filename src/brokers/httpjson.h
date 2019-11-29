/*
 * httpjson.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_HTTPJSON_H_
#define SRC_SIMPLEFX_HTTPJSON_H_

#include <string_view>
#include <imtjson/value.h>
#include <simpleServer/http_client.h>

class HTTPJson {
public:

	HTTPJson(simpleServer::HttpClient &&httpc, const std::string_view &baseUrl);
	void setToken(const std::string_view &token);


	json::Value GET(const std::string_view &path,
			json::Value &&headers = json::Value(),
			unsigned int expectedCode = 0);

	json::Value SEND(const std::string_view &path,
					const std::string_view &method,
					const json::Value &data,
					json::Value &&headers = json::Value(),
					unsigned int expectedCode = 0);

	json::Value POST(const std::string_view &path,
			const json::Value &data,
			json::Value &&headers = json::Value(),
			unsigned int expectedCode = 0);

	json::Value PUT(const std::string_view &path,
			const json::Value &data,
			json::Value &&headers = json::Value(),
			unsigned int expectedCode = 0);

	json::Value DELETE(const std::string_view &path,
			const json::Value &data,
			json::Value &&headers = json::Value(),
			unsigned int expectedCode = 0);

protected:
	simpleServer::HttpClient httpc;
	std::string baseUrl;
};





#endif /* SRC_SIMPLEFX_HTTPJSON_H_ */
