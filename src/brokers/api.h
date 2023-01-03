/*
 * api.h
 *
 *  Created on: 8. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_BROKERS_API_H_
#define SRC_BROKERS_API_H_

#include <iostream>
#include <limits>

#include <imtjson/value.h>
#include "../main/apikeys.h"
#include "../main/ibrokercontrol.h"
#include "../main/istockapi.h"
#include "../main/sgn.h"
#include "../shared/refcnt.h"




class AbstractBrokerAPI: public IStockApi, public IApiKey, public IBrokerControl, public IHistoryDataSource {
public:

	AbstractBrokerAPI(const std::string &secure_storage_path,
			const json::Value &apiKeyFormat);

	~AbstractBrokerAPI();
	///Called when mmbot is started with debug mode enabled
	/**
	 * @param enable if set to true, debug mode is enabled. The broker should send more debug informations
	 * to the stderr.
	 *
	 * Default implementaion only sets debug_mode flag to true, so any function can easily check this status
	 */
	virtual void enable_debug(bool enable);
	virtual BrokerInfo getBrokerInfo()  override {throw std::runtime_error("unsupported");}


	static void dispatch(std::istream &input, std::ostream &output, std::ostream &error, AbstractBrokerAPI &handler);


	///Called when new keys are set or loaded
	virtual void onLoadApiKey(json::Value keyData) = 0;

	///Called when broker should be initialized
	/** When broker starts, it cannot show any error message until it is requested for the very first time.
	 * This function is called before the first command is executed. If exception is throw, the exception is
	 * carried into caller and the broker can graceusly exit.
	 */
	virtual void onInit() = 0;


	virtual json::Value getSettings(const std::string_view & pairHint) const override;

	virtual json::Value setSettings(json::Value v) override;

	virtual void restoreSettings(json::Value v) override;

	virtual json::Value getMarkets() const override;

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


	virtual void setApiKey(json::Value keyData) override;
	virtual json::Value getApiKeyFields() const override;

	virtual void loadKeys();

	///Safely logs message
	/** Logging directly to stderr is discouradged because logging must be performed during
	 * a communication. This function collects log messages when happen outside communication
	 * and flushes them immediatelly when communication is opened
	 * @param msg
	 */
	void logMessage(std::string &&msg);


	///Requests to create subaccount
	/** The implementation can simply create new instance of itself. If subaccounts are not supported, then nullptr must be returned */
	virtual AbstractBrokerAPI *createSubaccount(const std::string &secure_storage_path) {return nullptr;}

	virtual PageData fetchPage(const std::string_view &method, const std::string_view &vpath, const PageData &pageData) override;


	virtual json::Value callMethod(std::string_view name, json::Value args);

	///Request more time for processing current command (to prevent timeout);
	void need_more_time();

	virtual json::Value getWallet_direct();
	virtual AllWallets getWallet() override;
	virtual json::Value testCall(const std::string_view &method, json::Value args) ;
	virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) override;
	virtual std::uint64_t downloadMinuteData(const std::string_view &asset,
					  const std::string_view &currency,
					  const std::string_view &hint_pair,
					  std::uint64_t time_from,
					  std::uint64_t time_to,
					  HistData &data
				) override;


	bool binary_mode = false;
	///tests, whether keys are valid
	///default implementation calls getWallet_direct(), as the feature is not implemented on brokers yet
	///however, this should be improved later
	///Keys are not valid when getWallet fails
	virtual void probeKeys();

	virtual bool reset() = 0;


protected:
	bool debug_mode = false;
	std::string secure_storage_path;
	json::Value apiKeyFormat;
	std::vector<std::string> logMessages;
	std::ostream *logStream = nullptr;;
	std::ostream *outStream = nullptr;;
	virtual void flushMessages();
	void connectStreams(std::ostream &log, std::ostream &out);
	void disconnectStreams();


	class LogProvider;
	ondra_shared::RefCntPtr<LogProvider> logProvider;

	friend json::Value handleSubaccount(AbstractBrokerAPI &handler, const json::Value &req);

	///function is not used here
	virtual void reset(const std::chrono::system_clock::time_point &tp) override {}

	///obsolete for compatibility
	virtual double getFees(const std::string_view &) {return 0;}

};



#endif /* SRC_BROKERS_API_H_ */

