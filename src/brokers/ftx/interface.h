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

#include <imtjson/string.h>

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
protected:
	mutable HTTPJson api;

	using SymbolMap = ondra_shared::linear_map<std::string, MarketInfo, std::less<std::string_view> >;
	using Positions = ondra_shared::linear_map<std::string, double, std::less<std::string_view> >;
	SymbolMap smap;

	void updatePairs();

	struct AccountInfo {
		double fees;
		double colateral;
		double leverage;
		Positions positions;
	};


	const AccountInfo &getAccountInfo();

	bool hasKey() const;

	std::string api_key, api_secret, api_subaccount;

	std::optional<AccountInfo> curAccount;
	using BalanceMap = ondra_shared::linear_map<std::string, double, std::less<std::string_view> >;
	BalanceMap balances;



	json::Value requestGET(std::string_view path);
	json::Value requestPOST(std::string_view path, json::Value params);
	json::Value requestDELETE(std::string_view path);

	json::Value signHeaders(const std::string_view &method,
						    const std::string_view &path,
							const json::Value &body);

	std::int64_t time_diff = 0;
	std::int64_t time_sync = 0;
	int order_nonce;
	void setTime(std::int64_t t);
	std::int64_t now();

	int genOrderNonce();

	static json::Value parseClientId(json::Value v);
	json::Value buildClientId(json::Value v);
};


#endif /* SRC_BITFINEX_INTERFACE_H_ */
