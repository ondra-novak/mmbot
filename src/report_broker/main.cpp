/*
 * main.cpp
 *
 *  Created on: 13. 12. 2019
 *      Author: ondra
 */
#include <iostream>

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
	std::string postUrl;
	std::string postMethod;
	std::string getUrl;
	std::string ident;
	std::string tmpfile;
};


std::string replaceIdent(std::string url, const std::string &ident) {
	auto p = url.find("%ident%");
	while (p != url.npos) {
		url = url.substr(0,p)+urlDecode(ident)+url.substr(p+7);
		 p = url.find("%ident%", p+7);
	}
	return url;
}

Config loadConfig(const IniConfig::Section &sect) {
	Config c;
	c.getUrl = sect.mandatory["get_url"].getString();
	c.postUrl = sect.mandatory["post_url"].getString();
	c.postMethod = sect.mandatory["post_method"].getString();
	c.ident = sect.mandatory["ident"].getString();
	c.tmpfile = sect.mandatory["work_file"].getPath();

	c.getUrl = replaceIdent(c.getUrl, c.ident);
	c.postUrl = replaceIdent(c.postUrl, c.ident);
	return c;
}

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

static std::mutex lock;

void storeToSend(const Config &cfg, Value data) {
	std::unique_lock<std::mutex> _(lock);
	data = data.replace("ident",cfg.ident);
	std::ofstream f(cfg.tmpfile, std::ios::out|std::ios::app);
	data.toStream(f);
	f << std::endl;
}

std::vector<Value> readStorage(std::string fname) {
	std::ifstream f(fname, std::ios::in);
	std::vector<Value>  out;
	if (!!f) {
		Value v = readFromStream(f);
		while (v.hasValue()) {
			out.push_back(v);
			v = readFromStream(f);
		}
	}
	return out;
}

ondra_shared::MsgQueue<std::string> errors;

void flushStoragePart(HTTPJson &httpc, const Config &cfg) {
	std::string part = cfg.tmpfile+".part";

	auto items = readStorage(part);
	for (Value v : items) {
		try {
			httpc.SEND(cfg.postUrl, cfg.postMethod, v, Object("Content-Type","application/json"));
		} catch (std::exception &e) {
			errors.push(e.what());
			storeToSend(cfg,v);
		}
	}
	std::remove(part.c_str());
}

void flushStorage(HTTPJson &httpc, const Config &cfg) {

	std::unique_lock<std::mutex> _(lock);
	std::string part = cfg.tmpfile+".part";
	std::rename(cfg.tmpfile.c_str(), part.c_str());
	_.unlock();

	flushStoragePart(httpc, cfg);

}

Value getReport(HTTPJson &httpc, const Config &cfg) {
	return httpc.GET(cfg.getUrl, Object("Accept","application/json"));
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

		Worker wrk = Worker::create(1);

		wrk >> [&]{
			flushStoragePart(httpc,cfg);
		};

		Value req = readFromStream(std::cin);
		Value resp;
		while (req.defined()) {
			try {
				errors.try_pump([](const std::string &v) {
					std::cerr << v << std::endl;
				});
				auto cmd = req[0].getString();
				if (cmd == "sendItem") {
					Value data = req[1];
					storeToSend(cfg, data);
					resp = Value(json::array,{true});
				} else if (cmd == "getReport") {
					Value rep = getReport(httpc,cfg);
					resp = {true, rep};
				} else if (cmd == "flush") {
					resp = {true};
				} else {
					throw std::runtime_error("unsupported function");
				}
				wrk >> [&] {
					flushStorage(httpc, cfg);
				};
			} catch (const std::exception &e) {
				resp = {false, e.what()};
			}
			resp.toStream(std::cout);
			std::cout << std::endl;
			req = readFromStream(std::cin);
		}

		//wait for finish all tasks
		{
			Countdown ct(1);
			wrk >> [&]{ct.dec();};
			ct.wait();
		}

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}





}
