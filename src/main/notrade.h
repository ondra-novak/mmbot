/*
 * notrade.h
 *
 *  Created on: 9. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_NOTRADE_H_
#define SRC_MAIN_NOTRADE_H_
#include "../shared/linear_map.h"
#include "istockapi.h"

class NoTrade: public IStockApi {
public:


	virtual double getBalance(const std::string_view & symb) ;
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) ;
	virtual Orders getOpenOrders(const std::string_view & par) ;
	virtual Ticker getTicker(const std::string_view & piar) ;
	virtual json::Value placeOrder(const std::string_view & pair, const Order &order) ;
	virtual bool reset() ;
	virtual bool isTest() const ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair);
	virtual double getFees(const std::string_view &pair);
	virtual std::vector<std::string> getAllPairs();



	ondra_shared::linear_map<std::string, double> priceMap;
};

#endif /* SRC_MAIN_NOTRADE_H_ */
