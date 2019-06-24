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
	OrderDataDB(std::string path);
	~OrderDataDB();

	void storeOrderData(json::Value orderId, json::Value data);
	void commit();
	json::Value getOrderData(const json::Value &orderId);
	bool load();


protected:
	std::string path;
	std::string lock_path;
	int lockfile;
	std::ofstream nextRev;
	std::unordered_map<json::Value, json::Value> curMap;

};

#endif /* SRC_POLONIEX_ORDERDATADB_H_ */
