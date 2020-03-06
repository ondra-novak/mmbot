/*
 * orderdatadb.h
 *
 *  Created on: 7. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_POLONIEX_ORDERDATADB_H_
#define SRC_POLONIEX_ORDERDATADB_H_
#include <string>
#include <fstream>
#include <unordered_map>
#include <imtjson/value.h>

class OrderDataDB {
public:
	OrderDataDB(std::string path, unsigned int maxRows = 500);
	~OrderDataDB();

	void store(json::Value orderId, json::Value data);
	void mark(json::Value orderId);
	json::Value get(json::Value pair);
protected:
	std::string frontFile, backFile;
	std::string lock_path;
	unsigned int curRows = 0;
	unsigned int maxRows = 0;
	std::unordered_map<json::Value, json::Value> curMap;
	unsigned int load(const std::string &file);
	int lockfile;

};

#endif /* SRC_POLONIEX_ORDERDATADB_H_ */
