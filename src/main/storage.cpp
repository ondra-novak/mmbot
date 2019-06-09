/*
 * storage.cpp
 *
 *  Created on: 16. 5. 2019
 *      Author: ondra
 */

#include "storage.h"

#include <fstream>
#include <experimental/filesystem>
#include <stack>

#include <imtjson/binjson.tcc>

using namespace std::experimental::filesystem;

Storage::Storage(std::string file, int versions, Format format):file(file),versions(versions),format(format) {
}

void Storage::store(json::Value data) {
	std::string tmpname = file+".tmp";
	std::ofstream f(tmpname, std::ios::out|std::ios::trunc);
	if (!f) throw std::runtime_error("Can't open the storage: "+file);
	switch(format) {
	case binjson:
		data.serializeBinary([&](char c) {f.put(c);},json::compressKeys);
		break;
	case jsonp:
		f << "fetch_callback(\"" << path(file).filename().string() << "\"," << std::endl;
		data.toStream(f);
		f << std::endl << ");" << std::endl;
		break;
	default:
	case json:
		data.toStream(f);;
		break;
	}

	f.close();

	std::stack<std::string> names;
	names.push(file);

	for (int i = 1; i < versions; i++) {
		std::ostringstream buff;
		buff << file << "~" << i;
		names.push(buff.str());
	}

	std::string to = names.top();
	names.pop();
	while (!names.empty()) {
		std::error_code ec;
		std::string from = names.top();
		names.pop();
		rename(from, to, ec);
		to = from;
	}
	rename(tmpname, to);
}

json::Value Storage::load() {
	std::ifstream f(file, std::ios::in);
	if (!f) {
		f.clear(f.failbit);
		f.open(file+"~1", std::ios::in);
		if (!f) return json::Value();
	}
	int x = f.get();
	if (x != EOF) f.putback(x);
	if (x == '{' ) {
		return json::Value::fromStream(f);
	} else {
		return json::Value::parseBinary([&] {return f.get();}, json::base64);
	}
}

PStorage StorageFactory::create(std::string name) const {
	return std::make_unique<Storage>(path+"/"+ name, versions, format);
}

