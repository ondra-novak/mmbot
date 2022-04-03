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
#include <userver/http_client.h>



class HTTPJson {
public:

	HTTPJson(const std::string_view &baseUrl);
	void setToken(const std::string_view &token);


	json::Value GET(const std::string_view &path,
			json::Value &&headers = json::Value(),
			unsigned int expectedCode = 0);
	json::Value GETq(const std::string_view &path,const json::Value &query,
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


	class UnknownStatusException: public std::exception {
	public:
		UnknownStatusException(int code, const std::string &message, json::Value body, json::Value headers)
			:code(code),message(message),body(body),headers(headers) {}
		UnknownStatusException(int code, std::string &&message, json::Value body, json::Value headers)
			:code(code),message(std::move(message)),body(body),headers(headers) {}

		const int code;
		const std::string message;
		const json::Value body;
		const json::Value headers;
		mutable std::string whatMsg;

		const virtual char* what() const noexcept override;
	};


	void setBaseUrl(const std::string &url);
	userver::HttpClient &getClient() {return httpc;}

	const auto &getLastServerTime() const {return lastServerTime;}
	std::chrono::system_clock::time_point now();

	void set_reading_fn(std::function<void()> reading_fn) {this->reading_fn = reading_fn;}
	void setForceJSON(bool force) {force_json = force;}
	bool getForceJSON() const {return force_json;}

	static void buildQuery(const json::Value items, std::string &out, std::string_view prefix = std::string_view());
	static std::string urlEncode(std::string_view x);

protected:
	userver::HttpClient httpc;
	std::string baseUrl;
	std::size_t baseUrlSz;
	std::chrono::system_clock::time_point lastServerTime;
	std::chrono::steady_clock::time_point lastLocalTime;
	std::function<void()> reading_fn;
	bool force_json = false;
	std::vector<char> buffer;



	static bool parseHttpDate(const std::string_view &date, std::chrono::system_clock::time_point & tp);
	json::Value parseResponse(userver::PHttpClientRequest &resp, json::Value &headers);
};





#endif /* SRC_SIMPLEFX_HTTPJSON_H_ */
