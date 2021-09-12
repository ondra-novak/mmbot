/*
 * bybitbroker.h
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */

#include <shared/linear_map.h>
#include "../api.h"
#include "../timesync.h"
#include "../httpjson.h"

#ifndef SRC_BROKERS_BYBIT_BYBITBROKER_H_
#define SRC_BROKERS_BYBIT_BYBITBROKER_H_

class ByBitBroker: public AbstractBrokerAPI {
public:
	ByBitBroker(const std::string &secure_storage_path);
	virtual void onInit();
	virtual double getBalance(const std::string_view & symb) {return 0;}
	virtual json::Value testCall(const std::string_view &method, json::Value args) override;
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
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
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual IBrokerControl::AllWallets getWallet() override;
protected:
	bool hasKeys() const;
	TimeSync curTime;
	mutable HTTPJson api;

	struct MarketInfoEx: IStockApi::MarketInfo {
		std::string alias;
		std::string expiration;
	};

	using SymbolMap=ondra_shared::linear_map<std::string, MarketInfoEx , std::less<> > ;
	mutable SymbolMap symbols;
	mutable std::chrono::steady_clock::time_point symbols_expiration;
	void updateSymbols() const;
	void forceUpdateSymbols() const ;
	json::Value publicGET(std::string_view uri) const;
	static void handleError(json::Value err) ;
	static void handleException(const HTTPJson::UnknownStatusException &ex) ;

	bool isInverted(const std::string_view &name) const;
	const MarketInfoEx &getSymbol(const std::string_view &name) const;


	std::string api_key, api_secret;
	bool testnet = false;
};

#endif /* SRC_BROKERS_BYBIT_BYBITBROKER_H_ */
