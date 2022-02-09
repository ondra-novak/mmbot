/*
 * interface.h
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */

#ifndef SRC_BITFINEX_INTERFACE_H_
#define SRC_BITFINEX_INTERFACE_H_
#include <chrono>
#include <optional>

#include "../api.h"
#include "../httpjson.h"
#include "../orderdatadb.h"
#include "structs.h"


class Interface:public AbstractBrokerAPI  {
public:
	Interface(const std::string &secure_storage_path);
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual BrokerInfo getBrokerInfo() override;
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
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual json::Value getMarkets() const override;
	virtual json::Value getWallet_direct() override;
	virtual bool areMinuteDataAvailable(const std::string_view &asset,
			const std::string_view &currency) override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
			std::vector<IHistoryDataSource::OHLC> &data) override;

protected:
	mutable HTTPJson api_pub;
	mutable HTTPJson api;
	mutable PairList pairList;
	mutable std::chrono::system_clock::time_point pairListExpire;

	const PairList& getPairs() const;
	static bool isMarginPair(const std::string_view &name);
	static std::string_view stripMargin(const std::string_view &name);
	const bool hasKey() const;

	using Wallet=::Wallet;

	std::string keyId, keySecret;
	std::optional<Wallet> wallet;
	std::optional<MarginBalance> marginBalance;
	std::optional<Positions> positions;
	std::map<std::string, Ticker, std::less<> > tickers;
	std::map<std::string, double, std::less<> > fees;
	std::map<std::string, double, std::less<> > curStep;
	OrderDataDB orderDB;

	json::Value signRequest(const std::string_view path, json::Value body) const;
	mutable std::uint64_t nonce;
	int order_nonce;
	bool needUpdateTickers=true;

	void updateTickers();
	void updateTicker(json::Value v);

	int genOrderNonce();

	double getFeeFromTrade(json::Value trade, const PairInfo &pair);
	json::Value signedPOST(std::string_view path, json::Value body) const;
	json::Value publicGET(std::string_view path)  const;
};


#endif /* SRC_BITFINEX_INTERFACE_H_ */
