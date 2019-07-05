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

	///Called when mmbot is started with debug mode enabled
	/**
	 * @param enable if set to true, debug mode is enabled. The broker should send more debug informations
	 * to the stderr.
	 *
	 * Default implementaion only sets debug_mode flag to true, so any function can easyli check this status
	 */
	virtual void enable_debug(bool enable) {debug_mode = enable;}


	static void dispatch(std::istream &input, std::ostream &output, IStockApi &handler);


	void dispatch();


	template<typename T, typename Fn>
	T mapJSON(json::Value cont, Fn &&fn, T &&tmp = T()) {
		for (json::Value x: cont) {
			tmp.push_back(fn(x));
		}
		return T(std::move(tmp));
	}

	template<typename Iter, typename T, typename Fn>
	T map(Iter itr, Iter end, Fn &&fn, T &&tmp = T()) {
		while (itr != end) {
			tmp.push_back(fn(*itr));
			++itr;
		}
		return T(std::move(tmp));

	}

	virtual bool isTest() const override {
		return false;
	}
	virtual void testBroker() override {}



protected:
	bool debug_mode = false;


};



#endif /* SRC_BROKERS_API_H_ */
