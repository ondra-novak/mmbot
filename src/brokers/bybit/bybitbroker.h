/*
 * bybitbroker.h
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */

#include <map>
#include <shared/linear_map.h>
#include "../api.h"
#include "../timesync.h"
#include "../httpjson.h"

#ifndef SRC_BROKERS_BYBIT_BYBITBROKER_H_
#define SRC_BROKERS_BYBIT_BYBITBROKER_H_

class ByBitBroker: public AbstractBrokerAPI {
public:
	ByBitBroker(const std::string &secure_storage_path);
	virtual void onInit() override;
	virtual double getBalance(const std::string_view & symb) {return 0;}
	virtual json::Value testCall(const std::string_view &method, json::Value args) override;
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual IBrokerControl::BrokerInfo getBrokerInfo() override;
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
	virtual bool areMinuteDataAvailable(const std::string_view &asset,
			const std::string_view &currency) override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
			std::vector<double> &data) override;

protected:
	bool hasKeys() const;
	mutable TimeSync curTime;
	mutable HTTPJson api;
	std::uint64_t nonce;

	enum MarketType {
		inverse_perpetual,
		usdt_perpetual,
		inverse_futures,
		spot
	};

	struct MarketInfoEx: IStockApi::MarketInfo {
		MarketType type;
		std::string name;
		std::string alias;
		std::string expiration;
	};

	using SymbolMap=ondra_shared::linear_map<std::string, MarketInfoEx , std::less<> > ;
	mutable SymbolMap symbols;
	mutable std::chrono::steady_clock::time_point symbols_expiration;
	void updateSymbols() const;
	void forceUpdateSymbols() const ;
	json::Value publicGET(std::string_view uri) const;
	json::Value privateGET(std::string_view uri, json::Value params);
	json::Value privatePOST(std::string_view uri, json::Value params);
	json::Value privatePOSTSpot(std::string_view uri, json::Value params);
	json::Value privateDELETESpot(std::string_view uri, json::Value params);
	std::string signRequest(json::Value &params);
	static void handleError(json::Value err) ;
	static void handleException(const HTTPJson::UnknownStatusException &ex) ;

	bool isInverted(const std::string_view &name) const;
	const MarketInfoEx &getSymbol(const std::string_view &name) const;


	std::string api_key, api_secret;
	bool testnet = false;

	std::map<std::string, json::Value, std::less<>> positionCache, walletCache;
	json::Value spotBalanceCache;
	json::Value getInversePerpetualPosition(std::string_view symbol);
	json::Value getUSDTPerpetualPosition(std::string_view symbol);
	json::Value getInverseFuturePosition(std::string_view symbol);
	json::Value getWalletState(std::string_view coin);
	json::Value getSpotBalance(std::string_view coin);
	json::Value composeOrderID(json::Value userData);
};

#endif /* SRC_BROKERS_BYBIT_BYBITBROKER_H_ */
