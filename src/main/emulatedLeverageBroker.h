/*
 * EmulatedLeverageBroker.h
 *
 *  Created on: 17. 12. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_EMULATEDLEVERAGEBROKER_H_
#define SRC_MAIN_EMULATEDLEVERAGEBROKER_H_

#include "ibrokercontrol.h"
#include "istockapi.h"

class EmulatedLeverageBroker: public IStockApi,public IBrokerControl		{
public:
	EmulatedLeverageBroker(PStockApi target, double emulatedLeverage);
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method,
			const std::string_view&vpath, const IBrokerControl::PageData &pageData) override;
	virtual json::Value getSettings(const std::string_view &pairHint) const override;
	virtual BrokerInfo getBrokerInfo() override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual json::Value setSettings(json::Value v) override;
	virtual void restoreSettings(json::Value v) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual void reset(const std::chrono::system_clock::time_point &tp) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize) override;
	virtual double getFees(const std::string_view &pair) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual json::Value getMarkets() const override;
	virtual AllWallets getWallet() override;

protected:
	PStockApi target;
	MarketInfo minfo;
	double emulatedLeverage;
};

#endif /* SRC_MAIN_EMULATEDLEVERAGEBROKER_H_ */
