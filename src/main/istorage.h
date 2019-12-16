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
	virtual PStorage create(std::string name) const = 0;
	virtual ~IStorageFactory() {}
};

using PStorageFactory = std::unique_ptr<IStorageFactory>;

#endif /* SRC_MAIN_ISTORAGE_H_ */
