#ifndef SRC_BROKERS_KUCOIN_KUCOIN_H_
#define SRC_BROKERS_KUCOIN_KUCOIN_H_

#include <map>
#include "../api.h"
#include "../orderdatadb.h"
#include <imtjson/value.h>
#include <shared/linear_map.h>
#include "../httpjson.h"

using json::Value;

class TradeOgreIFC: public AbstractBrokerAPI {
public:
	TradeOgreIFC(const std::string &cfg_file);
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual AbstractBrokerAPI* createSubaccount(
			const std::string &secure_storage_path) override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual IStockApi::BrokerInfo getBrokerInfo() override;
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
	mutable HTTPJson api;
	mutable std::string uriBuffer;
	OrderDataDB orderDB;
protected:
	Value publicGET(const std::string_view &uri, Value query) const;
	Value privateGET(const std::string_view &uri, Value query) const;
	Value privatePOST(const std::string_view &uri, Value args) const;
	const std::string &buildUri(const std::string_view &uri, Value query) const;

	struct MarketInfoEx: public MarketInfo {
	};

	Value orderCache;
	Value balanceCache;

	using SymbolMap = std::map<std::string, MarketInfoEx, std::less<> >;
	mutable SymbolMap symbolMap;


	mutable std::chrono::system_clock::time_point symbolExpires;
	bool firstReset = true;

	std::string api_key, api_secret;
	Value api_hdr;
	bool hasKey() const;

	void processError(const HTTPJson::UnknownStatusException &e) const;


	void updateSymbols() const;
	const MarketInfoEx &findSymbol(const std::string_view &name) const;



//	void generateTrades(json::Value orders, json::Value balances);
//	void createTrade(const std::string &market, double size,double price, int dir);
};




#endif /* SRC_BROKERS_KUCOIN_KUCOIN_H_ */
