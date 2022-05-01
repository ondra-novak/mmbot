/*
 * swap_broker.h
 *
 *  Created on: 4. 8. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_SWAP_BROKER_H_
#define SRC_MAIN_SWAP_BROKER_H_

#include <mutex>
#include <imtjson/value.h>
#include "abstractbrokerproxy.h"

#include "ibrokercontrol.h"
#include "istockapi.h"

class SwapBrokerBase: public AbstractBrokerProxy {
public:

	using AbstractBrokerProxy::AbstractBrokerProxy;

	virtual json::Value placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual void reset(const std::chrono::system_clock::time_point &tp) override;
protected:
	template<typename Fn>
	IStockApi::Orders getOpenOrders_p(Fn && priceConv, const std::string_view &par) ;
	template<typename Fn>
	IStockApi::TradesSync syncTrades_p(Fn && priceConv, json::Value lastId, const std::string_view &pair) ;
	template<typename Fn>
	IStockApi::Ticker getTicker_p(Fn && priceConv, const std::string_view &piar);
	template<typename Fn>
	void batchPlaceOrder_p(Fn && priceConv, const NewOrderList &orders, ResultList &result);

	MarketInfo minfo;

	Orders ords;
	std::recursive_mutex mx;

	std::pair<MarketInfo,Ticker> getMarketInfoAndTicker(const std::string_view &pair) ;
	NewOrderList convord;
	ResultList convord_ret;
	std::vector<json::Value> filtered;
	ResultList tmp_ret;
	NewOrderList tmp_ord;

};

class InvertBroker: public SwapBrokerBase {
public:
	using SwapBrokerBase ::SwapBrokerBase ;

	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual void batchPlaceOrder(const NewOrderList &orders, ResultList &result) override;

protected:


};

class SwapBroker: public SwapBrokerBase {
public:
	using SwapBrokerBase ::SwapBrokerBase ;

	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual void batchPlaceOrder(const NewOrderList &orders, ResultList &result) override;

};

#endif /* SRC_MAIN_SWAP_BROKER_H_ */
