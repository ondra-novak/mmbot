/*
 * httpjson.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_HTTPJSON_H_
#define SRC_SIMPLEFX_HTTPJSON_H_

#include <chrono>
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


	class UnknownStatusException: public simpleServer::HTTPStatusException {
	public:
		UnknownStatusException(int code, const std::string &message, simpleServer::HttpResponse response)
			:simpleServer::HTTPStatusException(code,message),response(response) {}
		UnknownStatusException(int code, std::string &&message, simpleServer::HttpResponse response)
			:simpleServer::HTTPStatusException(code,std::move(message)),response(response) {}

		simpleServer::HttpResponse response;
	};


	void setBaseUrl(const std::string &url);
	simpleServer::HttpClient &getClient() {return httpc;}

	const auto &getLastServerTime() const {return lastServerTime;}
	std::chrono::system_clock::time_point now();

	void set_reading_fn(std::function<void()> reading_fn) {this->reading_fn = reading_fn;}
	void setForceJSON(bool force) {force_json = force;}
	bool getForceJSON() const {return force_json;}

	
protected:
	simpleServer::HttpClient httpc;
	std::string baseUrl;
	std::chrono::system_clock::time_point lastServerTime;
	std::chrono::steady_clock::time_point lastLocalTime;
	std::function<void()> reading_fn;
	bool force_json = false;


	static bool parseHttpDate(const std::string_view &date, std::chrono::system_clock::time_point & tp);
	json::Value parseResponse(simpleServer::HttpResponse &resp, json::Value &headers);
	std::string handleLocation(std::string_view url, simpleServer::HeaderValue loc);
};





#endif /* SRC_SIMPLEFX_HTTPJSON_H_ */
