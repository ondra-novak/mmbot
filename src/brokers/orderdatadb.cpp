/*
 * orderdatadb.cpp
 *
 *  Created on: 7. 6. 2019
 *      Author: ondra
 */

#include "orderdatadb.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <csignal>
using namespace json;

OrderDataDB::OrderDataDB(std::string path):path(path),lock_path(path+".lock") {
	lockfile = ::open(lock_path.c_str(),O_TRUNC|O_CREAT|O_RDWR, 0666);
	int r = ::flock(lockfile, LOCK_EX|LOCK_NB);
	if (r == -1) {
		close(lockfile);
		throw std::runtime_error("Unable to lock: " + lock_path + " - The database file is locked");
	}
}

OrderDataDB::~OrderDataDB() {
	close(lockfile);
	remove(lock_path.c_str());
}

void OrderDataDB::storeOrderData(json::Value orderId, json::Value data) {
	Value({orderId, data}).toStream(nextRev);
	nextRev << std::endl;
	nextRev.flush();
}

void OrderDataDB::commit() {
		nextRev.close();
		load();
		nextRev.open(path, std::ios::out|std::ios::trunc);
}

json::Value OrderDataDB::getOrderData(const json::Value& orderId) {
	auto iter = curMap.find(orderId);
	if (iter == curMap.end()) return Value();
	else return iter->second;
}

bool eatWhite(std::ifstream &in) {
	int k = in.get();
	while (k!= EOF && std::isspace(k) ) {
		k  = in.get();
	}
	if (k == EOF) return false;
	in.putback(k);
	return true;
}

bool OrderDataDB::load() {
	curMap.clear();
	std::ifstream f(path, std::ios::in);
	if (!f) return false;

	while (eatWhite(f)) {
		Value pr = Value::fromStream(f);
		Value key = pr[0];
		Value value = pr[1];
		curMap[key] = value;
	}

	return true;

}

