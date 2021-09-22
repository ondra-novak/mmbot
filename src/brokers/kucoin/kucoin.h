#ifndef SRC_BROKERS_KUCOIN_KUCOIN_H_
#define SRC_BROKERS_KUCOIN_KUCOIN_H_

#include "../api.h"
#include <imtjson/value.h>

using json::Value;

class KucoinIFC: public AbstractBrokerAPI {
public:
	KucoinIFC(const std::string &cfg_file);
	virtual std::vector<std::string> getAllPairs() override;
	virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency)
			override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to, std::vector<IHistoryDataSource::OHLC> &data) override;
	virtual void testBroker() override;
	virtual json::Value getMarkets() const override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual void onInit() override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual bool reset() override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize)
					override;
	virtual double getFees(const std::string_view &pair) override;
	virtual IBrokerControl::AllWallets getWallet() override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;

protected:
	Value publicGET(const std::string_view &uri, Value query);

};




#endif /* SRC_BROKERS_KUCOIN_KUCOIN_H_ */
