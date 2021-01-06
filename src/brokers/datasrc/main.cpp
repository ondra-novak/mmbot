/*
 * main.cpp
 *
 *  Created on: 27. 10. 2020
 *      Author: ondra
 */
#include <cctype>
#include <sstream>
#include <imtjson/value.h>
#include <imtjson/operations.h>
#include <simpleServer/urlencode.h>
#include <simpleServer/http_client.h>
#include <imtjson/string.h>
#include "../httpjson.h"
#include "shared/stringview.h"

using json::String;
using json::Value;
using ondra_shared::StrViewA;

static HTTPJson httpc(simpleServer::HttpClient(simpleServer::HttpClient::defUserAgent, simpleServer::newHttpsProvider(), nullptr,nullptr),"");

String transformString(StrViewA str, int (*fn)(int)) {
	return String(str.length, [&](char *s){
		for (char c: str) {
			*(s++) = fn(c);
		}
		return str.length;
	});
}

Value readPrices(const StrViewA &asset, const StrViewA &currency, std::uint64_t fromTime) {
	std::ostringstream url;

	url << "https://devel.novacisko.cz/prices/minute.php?asset="
			<< simpleServer::urlEncode(asset)
			<< "&currency=" << simpleServer::urlEncode(currency)
			<< "&from=" << fromTime;

	return httpc.GET(url.str()).map([&](Value v){return v[1];});

}

Value readSymbols() {
	return httpc.GET("https://devel.novacisko.cz/prices/minute.php").map([](Value v){
		return transformString(v.getKey(), &std::toupper);
	}, json::array);
}

int main(int argc, char **argv) {

	std::istream &input (std::cin);

	while(true) {
		int i = input.get();
		while (i != EOF && isspace(i)) i = input.get();
		if (i == EOF) break;
		input.putback(i);
		Value cmd = Value::fromStream(input);
		Value out;
		if (cmd[0].getString() == "minute") {
			try {
				json::Value args = cmd[1];

				String asset = transformString(args["asset"].getString(), &std::tolower);
				String currency = transformString(args["currency"].getString(), &std::tolower);
				auto fromTime = args["from"].getUIntLong();

				Value data = readPrices(asset, currency, fromTime);
				out = {true, data};
			} catch (std::exception &e) {
				out = {false, e.what()};
			}
		} else if (cmd[0].getString()=="symbols") {
			try {
				Value data = readSymbols();
				out = {true, data};
			} catch (std::exception &e) {
				out = {false, e.what()};
			}

		} else {
			out = {false, "Unknown command"};
		}

		out.toStream(std::cout);
		std::cout << std::endl;
	}
}

