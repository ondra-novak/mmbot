#ifndef SRC_BROKERS_KUCOIN_KUCOIN_H_
#define SRC_BROKERS_KUCOIN_KUCOIN_H_

#include "../api.h"
#include <imtjson/value.h>
#include <shared/linear_map.h>
#include "../httpjson.h"
#include <optional>

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
	virtual IBrokerControl::BrokerInfo getBrokerInfo() override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to, std::vector<IHistoryDataSource::OHLC> &data) override;
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
	virtual json::Value getApiKeyFields() const override;

protected:
	mutable HTTPJson api;
	mutable std::string uriBuffer;
protected:
	Value publicGET(const std::string_view &uri, Value query) const;
	Value privateGET(const std::string_view &uri, Value query) const;
	Value privatePOST(const std::string_view &uri, Value args) const;
	Value privateDELETE(const std::string_view &uri, Value query) const;
	Value signRequest(const std::string_view &method, const std::string_view &function, json::Value args) const;
	const std::string &buildUri(const std::string_view &uri, Value query) const;

	struct MarketInfoEx: public MarketInfo {
	};

	using SymbolMap = ondra_shared::linear_map<std::string, MarketInfoEx, std::less<> >;
	using BalanceMap = ondra_shared::linear_map<std::string, double, std::less<> >;
	using OrderMap = std::vector<std::pair<std::string,Order> >;
	mutable SymbolMap symbolMap;
	mutable BalanceMap balanceMap;
	mutable std::optional<OrderMap> orderMap;
	mutable std::chrono::system_clock::time_point symbolExpires;
	unsigned int nextId = 0;

	std::string api_passphrase, api_key, api_secret;
	bool hasKey() const;

	///Function returns, if request must be retried - otherwise it throws exception
	void processError(const HTTPJson::UnknownStatusException &e) const;
	json::Value processResponse(json::Value v) const;

	void updateSymbols() const;
	const MarketInfoEx &findSymbol(const std::string_view &name) const;
	void updateOrders();

	void updateSymbolFees(const std::string_view &name) ;
	void updateBalances();
	json::Value generateOid(Value clientId);
	Value parseOid(json::Value iod);

};




#endif /* SRC_BROKERS_KUCOIN_KUCOIN_H_ */
