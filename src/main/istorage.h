/*
 * istorage.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTORAGE_H_
#define SRC_MAIN_ISTORAGE_H_
#include <memory>


class IStorage {
public:

	virtual void store(json::Value data) = 0;
	virtual json::Value load() = 0;
	virtual void erase() = 0;
	virtual ~IStorage() {}

};


using PStorage = std::unique_ptr<IStorage>;


class IStorageFactory {
public:
	virtual PStorage create(const std::string_view &name) const = 0;
	virtual ~IStorageFactory() {}
};

using PStorageFactory = std::shared_ptr<IStorageFactory>;


///Two storages, first is primary, other is secondary
/**
 *  The object reads from primary and in case of failure, secondary is used
 *  Writing is done to both storages while only one can be sucessful
 */
class BackedStorage: public IStorage {
public:
	BackedStorage(PStorage &&primary, PStorage &&secondary):primary(std::move(primary)),secondary(std::move(secondary)) {}

	virtual void store(json::Value data) {
		try {primary->store(data);} catch (...) {secondary->store(data);throw;}
		try {secondary->store(data);} catch (...) {}
	}
	virtual json::Value load() {
		try {
			json::Value v = primary->load();
			if (v.defined()) return v;
			v = secondary->load();
			return v;
		} catch (...) {
			return secondary->load();
		}

	}
	virtual void erase() {
		primary->erase();
		secondary->erase();
	}
protected:
	PStorage primary, secondary;
};

class BackedStorageFactory: public IStorageFactory {
public:
	BackedStorageFactory(PStorageFactory &&primary, PStorageFactory &&secondary)
			:primary(std::move(primary))
			,secondary(std::move(secondary)) {}
	virtual PStorage create(const std::string_view &name) const override {
		return PStorage(new BackedStorage(primary->create(name), secondary->create(name)));
	}
protected:
	PStorageFactory primary,secondary;
};

#endif /* SRC_MAIN_ISTORAGE_H_ */
