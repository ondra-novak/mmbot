/*
 * emulator.h
 *
 *  Created on: 2. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EMULATOR_H_
#define SRC_MAIN_EMULATOR_H_
#include <optional>

#include "../shared/logOutput.h"
#include "istockapi.h"



class EmulatorAPI: public IStockApi {
public:

	EmulatorAPI(IStockApi &datasrc, double initial_currency);


	virtual double getBalance(const std::string_view & symb) override;
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override;
	virtual bool isTest() const override;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override;
	virtual double getFees(const std::string_view &pair) override;
	virtual std::vector<std::string> getAllPairs() override;
	virtual void testBroker() override {datasrc.testBroker();}
	virtual BrokerInfo getBrokerInfo() override;

	static std::string_view prefix;

protected:
	IStockApi &datasrc;
	std::size_t prevId = 0;

	std::size_t genID();

	Orders orders;
	TradeHistory trades;

	std::string balance_symb;
	std::string currency_symb;
	std::string pair;
	double balance;
	double currency;
	double initial_currency;
	double margin_currency = 0;
	bool initial_read_balance = true;
	bool initial_read_currency = true;
	bool margin = false;

	double readBalance(const std::string_view &symb, double defval);

	MarketInfo minfo;

	ondra_shared::LogObject log;

	void simulation(const Ticker &tk);

};


#endif /* SRC_MAIN_EMULATOR_H_ */
