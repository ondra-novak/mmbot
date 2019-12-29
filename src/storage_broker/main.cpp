/*
 * main.cpp
 *
 *  Created on: 13. 12. 2019
 *      Author: ondra
 */
#include <iostream>
#include <string_view>

#include <simpleServer/urlencode.h>
#include <imtjson/value.h>
#include <imtjson/object.h>
#include "../shared/default_app.h"
#include "../brokers/httpjson.h"
#include "../shared/countdown.h"
#include "../shared/msgqueue.h"
#include "../shared/worker.h"

using json::Object;
using json::Value;
using ondra_shared::Countdown;
using ondra_shared::DefaultApp;
using ondra_shared::IniConfig;
using namespace simpleServer;



struct Config {
	std::string getUrl;
	std::string putUrl;
	std::string delUrl;
	std::string putMethod;
	std::string delMethod;
	std::string ident;
};


Value readFromStream(std::istream &in) {
	int i = in.get();
	while (i != EOF) {
		if (!std::isspace(i)) {
			in.putback(i);
			Value req = Value::fromStream(in);
			return req;
		}
		i = in.get();
	}
	return json::undefined;
}



std::string replacePlaceholder(std::string url, std::string_view placeholder, const std::string &ident) {
	auto p = url.find(placeholder);
	while (p != url.npos) {
		url = url.substr(0,p)+urlDecode(ident)+url.substr(p+placeholder.length());
		 p = url.find(placeholder, p+placeholder.length());
	}
	return url;
}

Config loadConfig(const IniConfig::Section &sect) {
	Config c;
	c.getUrl = sect.mandatory["get_url"].getString();
	c.putUrl = sect.mandatory["put_url"].getString();
	c.delUrl = sect.mandatory["del_url"].getString();
	c.ident = sect.mandatory["ident"].getString();
	c.putMethod = sect.mandatory["put_method"].getPath();
	c.delMethod = sect.mandatory["del_method"].getPath();

	c.getUrl = replacePlaceholder(c.getUrl, "%ident%", c.ident);
	c.putUrl = replacePlaceholder(c.putUrl, "%ident%", c.ident);
	c.delUrl = replacePlaceholder(c.delUrl, "%ident%", c.ident);
	return c;
}

int main(int argc, char **argv) {
	try {
		if (argc < 2) {
			std::cerr << "need argument - path to config" << std::endl;
			return 1;
		}

		Config cfg;
		{
			IniConfig ini;
			ini.load(argv[1]);
			cfg = loadConfig(ini["server"]);
		}

		HTTPJson httpc(HttpClient("MMBot reporting client",newHttpsProvider(),newNoProxyProvider()),"");


		Value req = readFromStream(std::cin);
		Value resp;
		while (req.defined()) {
			try {
				auto cmd = req[0].getString();
				if (cmd == "store") {
					Value data = req[1];
					Value name = data[0];
					Value content = data[1];
					httpc.SEND(replacePlaceholder(cfg.putUrl,"%name%",name.getString()),cfg.putMethod,content);
					resp = Value(json::array,{true});
				} else if (cmd == "load") {
					Value name = req[1];
					Value r = httpc.GET(replacePlaceholder(cfg.getUrl,"%name%", name.getString()));
					resp = {true, r};
				} else if (cmd == "erase") {
					Value name = req[1];
					Value resp = httpc.SEND(replacePlaceholder(cfg.delUrl,"%name%", name.getString()),cfg.delMethod,"");
					resp = Value(json::array,{true});
				} else {
					throw std::runtime_error("unsupported function");
				}
			} catch (const std::exception &e) {
				resp = {false, e.what()};
			}
			resp.toStream(std::cout);
			std::cout << std::endl;
			req = readFromStream(std::cin);
		}

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}





}
