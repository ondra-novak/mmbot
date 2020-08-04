/*
 * swap_broker.h
 *
 *  Created on: 4. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_SWAP_BROKER_H_
#define SRC_MAIN_SWAP_BROKER_H_

#include <imtjson/value.h>
#include "ibrokercontrol.h"
#include "istockapi.h"

class SwapBroker: public IStockApi, public IBrokerControl {
public:
	SwapBroker(PStockApi target);
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method,
			const std::string_view&vpath, const IBrokerControl::PageData &pageData) override;
	virtual json::Value getSettings(const std::string_view &pairHint) const override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
	virtual void testBroker() override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual json::Value setSettings(json::Value v) override;
	virtual void restoreSettings(json::Value v) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual bool reset() override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize) override;
	virtual double getFees(const std::string_view &pair) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;

protected:
	PStockApi target;
	Orders ords;
	MarketInfo minfo;
};

#endif /* SRC_MAIN_SWAP_BROKER_H_ */
