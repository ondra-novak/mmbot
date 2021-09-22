/*
 * interface.h
 *
 *  Created on: 22. 5. 2021
 *      Author: ondra
 */

#ifndef SRC_BROKERS_SOUTHXCHANGE_INTERFACE_H_
#define SRC_BROKERS_SOUTHXCHANGE_INTERFACE_H_

#include <chrono>
#include "../api.h"
#include "../httpjson.h"
#include "../orderdatadb.h"

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
	virtual void onInit() override;;
	virtual bool areMinuteDataAvailable(const std::string_view &asset,
			const std::string_view &currency);
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
			std::vector<IHistoryDataSource::OHLC> &data);

protected:

	HTTPJson api;


	using MarketMap = std::vector<MarketInfo>;
	static bool marketMapCmp(const MarketInfo  &a, const MarketInfo &b);

	MarketMap markets;
	std::chrono::system_clock::time_point marketMapExp;
	void updateMarkets();

	std::string api_key, api_secret;

	std::uint64_t nonce;

	json::Value apiPOST(const std::string_view &uri, json::Value params);
	json::Value cacheBalance;
	json::Value cacheOrders;

	using TxMap = std::map<std::string, json::Value>;
	TxMap txCache;




	OrderDataDB orderDB;
};



#endif /* SRC_BROKERS_SOUTHXCHANGE_INTERFACE_H_ */
