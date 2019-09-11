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
	virtual ~IStorage() {}


};

using PStorage = std::unique_ptr<IStorage>;


#endif /* SRC_MAIN_ISTORAGE_H_ */
