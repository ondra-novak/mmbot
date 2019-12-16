/*
 * ext_storage.cpp
 *
 *  Created on: 16. 12. 2019
 *      Author: ondra
 */

#include "ext_storage.h"

ExtStorage::ExtStorage(const std::string_view &workingDir,
		const std::string_view &name, const std::string_view &cmdline)
:proxy(new Proxy(workingDir, name, cmdline))
{}

PStorage ExtStorage::create(std::string name) const {
	return PStorage(new Handle(name, proxy));
}

void ExtStorage::Proxy::store(const std::string &name, const json::Value &data) {
	jsonRequestExchange("store", {name, data});
}

json::Value ExtStorage::Proxy::load(const std::string &name) {
	try {
		return jsonRequestExchange("load", name);
	} catch (...) {
		return json::undefined;
	}
}

void ExtStorage::Proxy::erase(const std::string &name) {
	jsonRequestExchange("erase", name);
}

ExtStorage::Handle::Handle(std::string name, ondra_shared::RefCntPtr<Proxy> proxy)
:name(name),proxy(proxy) {}

void ExtStorage::Handle::store(json::Value data) {
	proxy->store(name, data);
}

json::Value ExtStorage::Handle::load() {
	return proxy->load(name);
}

ExtStorage::Handle::~Handle() {
}

void ExtStorage::Handle::erase() {
	return proxy->erase(name);
}

ExtStorage::~ExtStorage() {
}

ExtStorage::Proxy::~Proxy() {
}
