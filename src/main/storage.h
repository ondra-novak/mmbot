/*
 * storage.h
 *
 *  Created on: 16. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STORAGE_H_
#define SRC_MAIN_STORAGE_H_
#include <memory>
#include <mutex>
#include <filesystem>
#include <imtjson/value.h>
#include <stack>

#include "istorage.h"


class Storage: public IStorage {
public:

	enum Format {
		json,
		jsonp,
		binjson
	};


	Storage(std::string file, int versions, Format format);

	virtual void store(json::Value data) override;
	virtual json::Value load() const override;
	virtual void erase() override;


protected:
	std::string file;
	int versions;
	Format format;

	std::stack<std::string> generateNames();
};


class StorageFactory: public IStorageFactory {
public:

	StorageFactory(std::string path):path(path),versions(5),format(Storage::json) {}
	StorageFactory(std::string path, bool binary):path(path),versions(5),format(binary?Storage::binjson:Storage::json) {}
	StorageFactory(std::string path, int versions, Storage::Format format):path(path),versions(versions),format(format) {}
	virtual PStorage create(const std::string_view &name) const override;


protected:
	std::filesystem::path path;
	int versions;
	Storage::Format format;
};

class MemStorage: public IStorage {
public:
	virtual void erase();
	virtual json::Value load() const;
	virtual void store(json::Value data);

protected:
	json::Value data;
	mutable std::mutex lock;
};

#endif /* SRC_MAIN_STORAGE_H_ */
