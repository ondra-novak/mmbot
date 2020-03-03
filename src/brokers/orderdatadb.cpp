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
#include <cerrno>
#include <csignal>
#include <cstring>
using namespace json;

OrderDataDB::OrderDataDB(std::string path, unsigned int maxRows):lock_path(path+"-lock"),maxRows(maxRows) {
	lockfile = ::open(lock_path.c_str(),O_TRUNC|O_CREAT|O_RDWR, 0666);
	if (lockfile == -1) {
		throw std::runtime_error("Unable to open: " + lock_path + " - " + strerror(errno));
	}
	int r = ::flock(lockfile, LOCK_EX|LOCK_NB);
	if (r == -1) {
		close(lockfile);
		throw std::runtime_error("Unable to lock: " + lock_path + " - The database file is locked");
	}
	backFile = path+"-back";
	frontFile = path+"-front";
	load(backFile);
	curRows = load(frontFile);
}

OrderDataDB::~OrderDataDB() {
	close(lockfile);
	remove(lock_path.c_str());
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


void OrderDataDB::store(json::Value orderId, json::Value data) {
	curMap[orderId] = data;
	mark(orderId);
}

void OrderDataDB::mark(json::Value orderId) {
	auto iter = curMap.find(orderId);
	if (iter != curMap.end()) {
		std::ofstream f(frontFile, std::ios::app);
		Value({iter->first, iter->second}).toStream(f);
		f<<std::endl;
		curRows++;
		if (curRows >= maxRows) {
			curMap.clear();
			load(backFile);
			load(frontFile);
			rename(frontFile.c_str(), backFile.c_str());
			curRows = 0;
		}
	}
}

json::Value OrderDataDB::get( json::Value orderId) {
	auto iter = curMap.find(orderId);
	if (iter != curMap.end()) return iter->second;
	else return Value();

}

unsigned int OrderDataDB::load(const std::string &file) {
	std::ifstream in(file);
	if (!in) return 0;
	unsigned int cnt = 0;
	while (!!in) {
		try {
			json::Value p = json::Value::fromStream(in);
			curMap[p[0]] = p[1];
			cnt++;
		} catch (...) {

		}
		int c = in.get();
		while (c != EOF && isspace(c)) c = in.get();
		if (c == EOF) break;
		in.putback(static_cast<char>(c));
	}
	return cnt;
}
