#pragma once
#ifndef SRC_BROKERS_XTB_INTERFACE_H_
#define SRC_BROKERS_XTB_INTERFACE_H_

#include "../api.h"
#include "../../main/papertrading.h"

#include <optional>
class ReplayInterface: public AbstractBrokerAPI {
public:

    ReplayInterface(const std::string &secure_storage_path);
    virtual std::vector<std::string> getAllPairs() override;
    virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
    virtual AbstractBrokerAPI* createSubaccount(
            const std::string &secure_storage_path) override;
    virtual BrokerInfo getBrokerInfo() override;
    virtual void onLoadApiKey(json::Value keyData) override;
    virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
    virtual IStockApi::TradesSync syncTrades(json::Value lastId, const std::string_view &pair) override;
    virtual void onInit() override;
    virtual bool reset() override;
    virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
    virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
            json::Value clientId, json::Value replaceId, double replaceSize)
                    override;
    virtual double getFees(const std::string_view &pair) override;
    virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
    virtual json::Value getMarkets() const override;
    virtual AllWallets getWallet() override;
    virtual bool areMinuteDataAvailable(const std::string_view &asset,
            const std::string_view &currency) override;
    virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
            const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
            IHistoryDataSource::HistData &data) override;
    virtual void probeKeys() override;

    virtual json::Value setSettings(json::Value v) override;
    virtual void restoreSettings(json::Value v) override;
    virtual json::Value getSettings(const std::string_view &pairHint) const override;

protected:

    PaperTrading _paper;

    struct MarketInfoEx : MarketInfo {
        std::vector<double> data;
        std::chrono::system_clock::time_point start_time;
    };

    std::optional<MarketInfoEx> _market;

    class Source;

    virtual void setApiKey(json::Value keyData) override;
    const MarketInfoEx& get_market() const;
    Ticker create_ticker() const;
};



#endif /* SRC_BROKERS_XTB_INTERFACE_H_ */
