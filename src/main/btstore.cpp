/*
 * btstore.cpp
 *
 *  Created on: Aug 24, 2021
 *      Author: ondra
 */

#include <fstream>
#include <string>
#include <shared/filesystem.h>
#include "btstore.h"

#include <unistd.h>
#include <vector>

#include <imtjson/binjson.h>
#include <imtjson/binjson.tcc>
BacktestStorage::BacktestStorage( std::size_t max_files, bool in_memory)
	:max_files(std::max<std::size_t>(8,max_files))
	,in_memory(in_memory)
{
}

BacktestStorage::~BacktestStorage() {
	cleanup();
}


void BacktestStorage::cleanup() {
	if (!in_memory) {
		for (const auto &x: meta) {
			std::filesystem::remove(x.fpath);
		}
	} else {
		in_memory_files.clear();
	}
}

std::vector<BacktestStorage::Metadata>::const_iterator BacktestStorage::find(const std::string &id) const {
	auto r = std::lower_bound(meta.begin(), meta.end(), Metadata{id}, cmp_metadata);
	if (r == meta.end() || r->id != id) return meta.end();
	else return r;
}

bool BacktestStorage::cmp_metadata(const Metadata &m1, const Metadata &m2) {
	return m1.id < m2.id;
}

std::vector<BacktestStorage::Metadata>::const_iterator BacktestStorage::find_to_remove() const {
	auto mx = std::chrono::system_clock::now()-std::chrono::seconds(30);
	auto found = meta.end();
	for (auto iter = meta.begin(); iter != meta.end(); ++iter) {
		if (iter->lastAccess<mx) {
			mx = iter->lastAccess;
			found = iter;
		}
	}
	return found;
}

void BacktestStorage::add_metadata(const Metadata &md) {
	auto r = std::lower_bound(meta.begin(), meta.end(), md, cmp_metadata);
	if (r != meta.end() && r->id == md.id) {
		mark_access(r);
	} else {
		meta.insert(r, md);
		while (meta.size()>max_files) {
			auto rm = find_to_remove();
			if (rm != meta.end()) {
				remove_metadata(rm);
			} else {
				break;
			}
		}
	}
}


void BacktestStorage::remove_metadata(const std::vector<Metadata>::const_iterator &iter) {
	auto p = iter->fpath;
	meta.erase(iter);
	if (in_memory) {
		in_memory_files.erase(p);
	} else {
		std::filesystem::remove(p);
	}
}

void BacktestStorage::store_data(const json::Value &data, const std::string &id) {
	if (in_memory) {
		in_memory_files[id] =  data;
		add_metadata({id,id,std::chrono::system_clock::now()});
	} else {
		auto tmpPath = std::filesystem::temp_directory_path();
		std::string pid = std::to_string(getpid());
		auto fpath = tmpPath / ("mmbot_backtest_"+pid+"x"+id);
		std::ofstream f(fpath, std::ios::binary);
		if (!(!f)) {
			data.serializeBinary([&](char c){f.put(c);}, json::compressKeys);
			if (!(!f)) {
				add_metadata({id,fpath.string(),std::chrono::system_clock::now()});
				return;
			}
		}
		throw std::runtime_error("Inaccessible temporary storage");
	}
}

std::string BacktestStorage::store_data(const json::Value &data) {
	if (!data.defined()) return std::string();
	std::hash<json::Value> h;
	auto hval = h(data);
	std::string id = std::to_string(hval);
	store_data(data, id);
	return id;
}

json::Value BacktestStorage::load_data(const std::string &id) {
	auto iter = find(id);
	if (iter == meta.end()) return json::Value();
	if (in_memory) {
		mark_access(iter);
		return in_memory_files[iter->fpath];
	} else {
		std::ifstream f(iter->fpath, std::ios::binary);
		if (!(!f)) {
			json::Value v =json::Value::parseBinary([&](){return f.get();});
			if (!(!f)) {
				mark_access(iter);
				return v;
			}
		}
		remove_metadata(iter);
		return json::Value();
	}

}

void BacktestStorage::mark_access(const std::vector<Metadata>::const_iterator &iter) {
	auto pos = std::distance(meta.cbegin(), iter);
	auto myiter = meta.begin()+pos;
	myiter->lastAccess = std::chrono::system_clock::now();

}
