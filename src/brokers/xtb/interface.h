#pragma once
#ifndef SRC_BROKERS_XTB_INTERFACE_H_
#define SRC_BROKERS_XTB_INTERFACE_H_

#include "../api.h"

#include "client.h"

#include "ratio.h"

#include "position_control.h"

#include "assets.h"

#include "orders.h"
class XTBInterface: public AbstractBrokerAPI {
public:


    XTBInterface(const std::string &secure_storage_path);
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

public:
    simpleServer::HttpClient _httpc;
    std::unique_ptr<XTBClient> _client;
    std::shared_ptr<XTBOrderbookEmulator> _orderbook;
    std::unique_ptr<RatioTable> _rates;
    std::unique_ptr<XTBAssets> _assets;
    std::shared_ptr<PositionControl> _position_control;
    std::vector<PositionControl::Trade> _trades, _trades_tmp;

    double _equity;
    std::string _base_currency;
    bool _is_demo;

    void stop_client();
    bool logged_in() const;
    void test_login() const;
    void update_equity();
};



#endif /* SRC_BROKERS_XTB_INTERFACE_H_ */
