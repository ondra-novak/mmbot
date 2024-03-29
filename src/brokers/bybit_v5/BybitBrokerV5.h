/*
 * BybitBroker.h
 *
 *  Created on: 21. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_BROKERS_BYBIT_V5_BYBITBROKERV5_H_
#define SRC_BROKERS_BYBIT_V5_BYBITBROKERV5_H_

#include "../api.h"
#include "../httpjson.h"

#include "rsa_tools.h"
#include <memory>
#include <optional>
class ByBitBrokerV5: public AbstractBrokerAPI {
public:
    ByBitBrokerV5(std::string secure_path);

    virtual AbstractBrokerAPI *createSubaccount(const std::string &secure_storage_path) override;
    virtual BrokerInfo getBrokerInfo()  override;

    virtual void onLoadApiKey(json::Value keyData) override;
    virtual void onInit() override;

    virtual json::Value getMarkets() const override;
    virtual json::Value getApiKeyFields() const override;
    virtual AllWallets getWallet() override;
    virtual json::Value testCall(const std::string_view &method, json::Value args) ;
    virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) override;
    virtual std::uint64_t downloadMinuteData(const std::string_view &asset,
                      const std::string_view &currency,
                      const std::string_view &hint_pair,
                      std::uint64_t time_from,
                      std::uint64_t time_to,
                      HistData &data
                ) override;
    virtual void probeKeys() override;
    virtual bool reset() override;
    virtual IStockApi::TradesSync syncTrades(json::Value lastId,
            const std::string_view &pair) override;
    virtual json::Value placeOrder(const std::string_view &pair, double size,
            double price, json::Value clientId, json::Value replaceId,
            double replaceSize) override;
    virtual std::vector<std::string> getAllPairs() override;
    virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
    virtual IStockApi::Orders getOpenOrders(const std::string_view &par)override;
    virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair)override;
    virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
    virtual double getFees(const std::string_view&) override;


protected:
    enum class AccountType {
        unknown = 0,
        regular = 1,
        unified_margin = 2,
        unified_trade = 3
    };

    std::unique_ptr<HTTPJson> httpc;
    bool is_paper = false;
    AccountType unified_mode = AccountType::unknown;

    std::string cur_api_key;
    PEVP_PKEY cur_priv_key;

    bool has_keys() const;

    enum class Category {
        spot, linear, inverse
    };

    struct MarketInfoEx: MarketInfo {
        Category cat;
        std::string api_symbol;
        std::string future_id;
        std::string priceToString(double v) const;
        std::string sizeToString(double v) const;
    };

    using SymbolMap = std::map<std::string, MarketInfoEx, std::less<>>;

    SymbolMap _symbol_map;
    std::chrono::system_clock::time_point _symbol_map_expire;


    const SymbolMap &getSymbols();
    const MarketInfoEx &getSymbol(std::string_view symbol);

    json::Value publicGET(std::string path, json::Value query);
    json::Value privateGET(std::string path, json::Value query);
    json::Value privatePOST(std::string path, json::Value payload);
    static json::Value category_to_value(Category cat);
    AccountType getAccountType();

    static json::Value parseLinkId(json::Value linkId);
    static json::Value createLinkId(json::Value tag);
    static json::Value cat2Val(Category cat);
};


#endif /* SRC_BROKERS_BYBIT_V5_BYBITBROKERV5_H_ */
