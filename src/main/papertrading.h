/*
 * papertrading.h
 *
 *  Created on: 16. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_PAPERTRADING_H_
#define SRC_MAIN_PAPERTRADING_H_

#include <map>
#include <optional>

#include "ibrokercontrol.h"
#include "acb.h"

#include "istockapi.h"

class PaperTrading: public IStockApi, public IBrokerControl {
public:

	PaperTrading(PStockApi s);
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair);
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair);
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair);
	virtual bool reset();
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par);
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize);
	virtual double getFees(const std::string_view &pair);
	virtual IStockApi::Ticker getTicker(const std::string_view &piar);
	virtual json::Value getMarkets() const;
	virtual std::vector<std::string> getAllPairs();
	virtual void restoreSettings(json::Value v);
	virtual json::Value setSettings(json::Value v);
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method,
			const std::string_view &vpath, const IBrokerControl::PageData &pageData);
	virtual json::Value getSettings(const std::string_view &pairHint) const;
	virtual IBrokerControl::BrokerInfo getBrokerInfo();
	virtual IBrokerControl::AllWallets getWallet();

protected:
	PStockApi source;
	using Wallet = std::map<std::string, double, std::less<> >;

	json::Value exportState();
	void importState(json::Value v);

	Wallet wallet;
	TradeHistory trades;
	std::optional<MarketInfo> minfo;
	std::optional<ACB> collateral;
	Ticker ticker;
	Orders openOrders;
	int orderCounter;
	std::string pairId;
	bool needLoadWallet = true;
};



#endif /* SRC_MAIN_PAPERTRADING_H_ */
