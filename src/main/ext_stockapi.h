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
#include "apikeys.h"
#include "ibrokercontrol.h"



class ExtStockApi: public AbstractExtern, public IStockApi, public IApiKey, public IBrokerControl, public IBrokerIcon {
public:

	ExtStockApi(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline);



	virtual double getBalance(const std::string_view & symb) override;
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override;
	virtual double getFees(const std::string_view & pair) override;
	virtual std::vector<std::string> getAllPairs() override;
	virtual void testBroker() override {preload();}
	virtual void onConnect() override;
	virtual BrokerInfo getBrokerInfo()  override;
	virtual void setApiKey(json::Value keyData) override;
	virtual json::Value getApiKeyFields() const override;
	virtual json::Value getSettings(const std::string_view & pairHint) const override;
	virtual void setSettings(json::Value v) override;
	virtual void saveIconToDisk(const std::string &path) const override;
	virtual std::string getIconName() const override;


};





#endif /* SRC_MAIN_EXT_STOCKAPI_H_ */
