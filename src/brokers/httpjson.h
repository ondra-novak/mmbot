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

	enum BodyType {
		///do not set content type
		none,
		///set application/json and use toString to serialize string
		json,
		///set application/x-www-form-urlencoded and use json to query serializer
		form,
	};

	json::Value GET(const std::string_view &path, unsigned int expectedCode = 0);
	json::Value SEND(const std::string_view &path, const std::string_view &method, const json::Value &data,BodyType bodyType,  unsigned int expectedCode = 0);
	json::Value POST(const std::string_view &path, const json::Value &data, BodyType bodyType, unsigned int expectedCode = 0);
	json::Value PUT(const std::string_view &path, const json::Value &data, BodyType bodyType, unsigned int expectedCode = 0);
	json::Value DELETE(const std::string_view &path, const json::Value &data, BodyType bodyType, unsigned int expectedCode = 0);
	bool hasToken() {return !token.empty();}
protected:
	simpleServer::HttpClient httpc;
	std::string baseUrl;
	std::string token;
};





#endif /* SRC_SIMPLEFX_HTTPJSON_H_ */
