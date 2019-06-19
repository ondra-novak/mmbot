/*
 * ext_stockapi.h - external stock api
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EXT_STOCKAPI_H_
#define SRC_MAIN_EXT_STOCKAPI_H_

#include "istockapi.h"
#include "abstractExtern.h"



class ExtStockApi: public AbstractExtern, public IStockApi {
public:

	ExtStockApi(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline);



	virtual double getBalance(const std::string_view & symb) override;
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override;
	virtual bool isTest() const override {return false;}
	virtual MarketInfo getMarketInfo(const std::string_view & pair);
	virtual double getFees(const std::string_view & pair);
	virtual std::vector<std::string> getAllPairs() override;

};





#endif /* SRC_MAIN_EXT_STOCKAPI_H_ */
