/*
 * interface.h
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */

#ifndef SRC_KRAKEN_INTERFACE_H_
#define SRC_KRAKEN_INTERFACE_H_
#include <chrono>
#include <optional>

#include <imtjson/binary.h>
#include "../api.h"
#include "../httpjson.h"
#include "../orderdatadb.h"


class Interface:public AbstractBrokerAPI  {
public:
	Interface(const std::string &secure_storage_path);
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId, const std::string_view &pair) override;
	virtual void onInit() override;
	virtual bool reset() override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize)
					override;
	virtual double getFees(const std::string_view &pair) override;
	virtual double getBalance(const std::string_view &symb) override {return 0;}
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual json::Value getMarkets() const;
protected:
	mutable HTTPJson api;
	OrderDataDB orderDB;

	json::Value symbolMap;
	json::Value isymbolMap;
	json::Value pairMap;
	json::Value balanceMap;
	json::Value positionMap;
	std::chrono::system_clock::time_point mapExpire;
	json::Value tickerMap;
	json::Value orderMap;
	bool tickerValid = false;
	json::Value trades_cachedResponse;
	json::Value trades_cachedLastId;
	double fees = -1;

	std::string apiKey;
	json::Binary apiSecret;

	void updateSymbols();
	bool hasKey() const;

	std::uint64_t nonce;


	json::Value public_GET(std::string_view path);
	json::Value public_POST(std::string_view path, json::Value req);
	json::Value private_POST(std::string_view path, json::Value req);
	void processError(HTTPJson::UnknownStatusException &e);
	static json::Value checkError(json::Value v);

	enum class MarketType {
		exchange,
		leveraged,
		hybrid
	};


	static std::string_view stripPrefix(std::string_view pair);
	static MarketType getMarketType(std::string_view pair);

	double getSpotBalance(const std::string_view &symb) ;
	double getCollateral(const std::string_view &symb) ;
	double getPosition(const std::string_view &market) ;
	json::Value placeOrderImp(const std::string_view & pair, double size, double price, json::Value clientId, bool lev);
};

#endif
