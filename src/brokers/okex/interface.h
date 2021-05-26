/*
 * interface.h
 *
 *  Created on: 25. 5. 2021
 *      Author: ondra
 */

#ifndef SRC_BROKERS_OKEX_INTERFACE_H_
#define SRC_BROKERS_OKEX_INTERFACE_H_

#include <chrono>
#include "../api.h"
#include "../httpjson.h"
#include "../orderdatadb.h"


namespace okex {


class Interface: public AbstractBrokerAPI {
public:

	Interface(const std::string &config_path);
	virtual json::Value testCall(const std::string_view &method, json::Value args) override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
	virtual json::Value getMarkets() const override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual bool reset() override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize)
					override;
	virtual double getFees(const std::string_view &pair) override;
	virtual IBrokerControl::AllWallets getWallet() override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual std::vector<std::string> getAllPairs() override;;
	virtual void onInit() override;
	virtual double getBalance(const std::string_view & symb) override;
protected:

	mutable HTTPJson api;
	std::string api_key, api_secret, api_passphrase;

	mutable json::Value instr_cache;
	json::Value account_cache;

	json::Value authReq(std::string_view method, std::string_view uri, json::Value body) const;
	void updateAccountData();
	void updateInstruments() const;

	std::map<std::string, MarketInfo, std::less<> > mkcache;
	const MarketInfo &getMkInfo(const std::string_view &pair);

	std::map<std::string, json::Value, std::less<> > clientIdMap;

	std::string createTag(const json::Value &clientId);
	json::Value parseTag(const std::string_view &tag);

};


}


#endif /* SRC_BROKERS_OKEX_INTERFACE_H_ */
