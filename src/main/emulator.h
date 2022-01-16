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
#include "ibrokercontrol.h"
#include "istockapi.h"



class EmulatorAPI: public IStockApi, public IBrokerIcon {
public:

	EmulatorAPI(PStockApi datasrc, double initial_currency);


	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override;
	virtual double getFees(const std::string_view &pair) override;
	//saves image to disk to specified path
	virtual void saveIconToDisk(const std::string &path) const override;
	//retrieves name of saved image
	virtual std::string getIconName() const override;

	static std::string_view prefix;

protected:
	PStockApi datasrc;
	std::size_t prevId = 0;

	std::size_t genID();

	Orders orders;
	TradeHistory trades;
	Ticker lastTicker;

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

	double readBalance(const std::string_view &symb, const std::string_view & pair, double defval);

	MarketInfo minfo;

	ondra_shared::LogObject log;

	void simulation(const Ticker &tk);

};


#endif /* SRC_MAIN_EMULATOR_H_ */
