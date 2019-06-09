/*
 * emulator.h
 *
 *  Created on: 2. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EMULATOR_H_
#define SRC_MAIN_EMULATOR_H_
#include <optional>

#include "istockapi.h"



class EmulatorAPI: public IStockApi {
public:

	EmulatorAPI(IStockApi &datasrc);


	virtual double getBalance(const std::string_view & symb);
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair);
	virtual Orders getOpenOrders(const std::string_view & par);
	virtual Ticker getTicker(const std::string_view & piar);
	virtual json::Value placeOrder(const std::string_view & pair, const Order &order) ;
	virtual bool reset() ;
	virtual bool isTest() const ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair);
	virtual double getFees(const std::string_view &pair);
	virtual std::vector<std::string> getAllPairs() override;

protected:
	IStockApi &datasrc;
	std::size_t prevId = 0;

	std::size_t genID();

	Orders orders;
	TradeHistory trades;

	std::string balance_symb;
	std::string pair;
	double balance;

	MarketInfo minfo;


	void simulation(const Ticker &tk);

};


#endif /* SRC_MAIN_EMULATOR_H_ */
