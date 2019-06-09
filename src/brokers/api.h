/*
 * api.h
 *
 *  Created on: 8. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_BROKERS_API_H_
#define SRC_BROKERS_API_H_

#include <iostream>
#include "../main/istockapi.h"

class AbstractBrokerAPI: public IStockApi {
public:


	static void dispatch(std::istream &input, std::ostream &output, IStockApi &handler);


	void dispatch();


	template<typename T, typename Fn>
	T mapJSON(json::Value cont, Fn &&fn) {
		T tmp;
		for (json::Value x: cont) {
			tmp.push_back(fn(x));
		}
		return tmp;
	}

	virtual bool isTest() const override {
		return false;
	}


};



#endif /* SRC_BROKERS_API_H_ */
