/*
 * swap_broker.h
 *
 *  Created on: 4. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_SWAP_BROKER_H_
#define SRC_MAIN_SWAP_BROKER_H_

#include <imtjson/value.h>
#include "abstractbrokerproxy.h"

#include "ibrokercontrol.h"
#include "istockapi.h"

class InvertBroker: public AbstractBrokerProxy {
public:
	InvertBroker(PStockApi target);

	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual void reset(const std::chrono::system_clock::time_point &tp) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
    virtual IStockApi::TradingStatus getTradingStatus(const std::string_view &pair,
            json::Value instance) override;
    virtual void placeOrders(const std::string_view &pair, std::vector<IStockApi::OrderToPlace> &orders,
            json::Value &instance) override;

protected:
	Orders ords;
	MarketInfo minfo;

	std::pair<MarketInfo,Ticker> getMarketInfoAndTicker(const std::string_view &pair) ;
};

class SwapBroker: public InvertBroker {
public:
	using InvertBroker ::InvertBroker ;

	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;

};

#endif /* SRC_MAIN_SWAP_BROKER_H_ */
