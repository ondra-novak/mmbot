/*
 * btstore.h
 *
 *  Created on: Aug 24, 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_BTSTORE_H_
#define SRC_MAIN_BTSTORE_H_
#include <chrono>
#include <vector>

#include <imtjson/value.h>



class BacktestStorage {
public:
	BacktestStorage(std::size_t max_files);
	~BacktestStorage();

	void cleanup();
	std::string store_data(const json::Value &data);
	json::Value load_data(const std::string &id);


protected:
	std::size_t max_files;

	struct Metadata {
		std::string id;
		std::string fpath;
		std::chrono::system_clock::time_point lastAccess;
	};

	static bool cmp_metadata(const Metadata &m1, const Metadata &m2);

	std::vector<Metadata> meta;
	std::vector<Metadata>::const_iterator find(const std::string &id) const;
	std::vector<Metadata>::const_iterator find_to_remove() const;
	void add_metadata(const Metadata &md);
	void remove_metadata(const std::vector<Metadata>::const_iterator &iter);
	void mark_access(const std::vector<Metadata>::const_iterator &iter);


};

#endif /* SRC_MAIN_BTSTORE_H_ */
