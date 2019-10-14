/*
 * api.h
 *
 *  Created on: 8. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_BROKERS_API_H_
#define SRC_BROKERS_API_H_

#include <iostream>

#include <imtjson/value.h>
#include "../main/apikeys.h"
#include "../main/istockapi.h"



class AbstractBrokerAPI: public IStockApi, public IApiKey {
public:

	AbstractBrokerAPI(const std::string &secure_storage_path,
			const json::Value &apiKeyFormat);

	///Called when mmbot is started with debug mode enabled
	/**
	 * @param enable if set to true, debug mode is enabled. The broker should send more debug informations
	 * to the stderr.
	 *
	 * Default implementaion only sets debug_mode flag to true, so any function can easyli check this status
	 */
	virtual void enable_debug(bool enable) {debug_mode = enable;}

	virtual BrokerInfo getBrokerInfo()  override {throw std::runtime_error("unsupported");}


	static void dispatch(std::istream &input, std::ostream &output, AbstractBrokerAPI &handler);


	///Called when new keys are set or loaded
	virtual void onLoadApiKey(json::Value keyData) = 0;

	///Called when broker should be initialized
	/** When broker starts, it cannot show any error message until it is requested for the very first time.
	 * This function is called before the first command is executed. If exception is throw, the exception is
	 * carried into caller and the broker can graceusly exit.
	 */
	virtual void onInit() = 0;

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

	virtual void setApiKey(json::Value keyData) override;
	virtual json::Value getApiKeyFields() const override;

	virtual void loadKeys();


protected:
	bool debug_mode = false;
	std::string secure_storage_path;
	json::Value apiKeyFormat;

};



#endif /* SRC_BROKERS_API_H_ */
