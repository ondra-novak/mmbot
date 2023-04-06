#ifndef SRC_BROKERS_COINBASE_COINBASE_ADVANCED_H_
#define SRC_BROKERS_COINBASE_COINBASE_ADVANCED_H_

#include "../api.h"
#include "../httpjson.h"
#include "../isotime.h"
#include "../ws_support.h"

#include "orderbook.h"

#include "orderlist.h"

#include <future>
#include <optional>

class CoinbaseAdv: public AbstractBrokerAPI {
public:
    CoinbaseAdv(const std::string &path);

    virtual std::vector<std::string> getAllPairs() override;
    virtual AbstractBrokerAPI* createSubaccount(const std::string &secure_storage_path) override;
    virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) override;
    virtual json::Value placeOrder(const std::string_view &pair, double size,
            double price, json::Value clientId, json::Value replaceId,
            double replaceSize) override;
    virtual json::Value getMarkets() const override;
    virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
    virtual void onInit() override;
    virtual json::Value getSettings(const std::string_view &pairHint) const override;
    virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
    virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
    virtual uint64_t downloadMinuteData(const std::string_view &asset,
            const std::string_view &currency, const std::string_view &hint_pair,
            uint64_t time_from, uint64_t time_to,
            HistData &data) override;
    virtual IBrokerControl::AllWallets getWallet() override;
    virtual IStockApi::TradesSync syncTrades(json::Value lastId,
            const std::string_view &pair) override;
    virtual void onLoadApiKey(json::Value keyData) override;
    virtual IBrokerControl::BrokerInfo getBrokerInfo() override;
    virtual json::Value testCall(const std::string_view &method,
            json::Value args) override;
    virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
    virtual json::Value setSettings(json::Value v) override;
    virtual void restoreSettings(json::Value v) override;
    virtual bool reset() override;
protected:
    mutable HTTPJson httpc;

    class MyWsInstance: public WsInstance {
    public:
        MyWsInstance(CoinbaseAdv &owner, simpleServer::HttpClient &client, std::string url);
        virtual json::Value generate_headers() override;
    protected:
        CoinbaseAdv &_owner;
    };

    class MyOrderList: public OrderList {
    public:
        MyOrderList(CoinbaseAdv &owner):_owner(owner) {}
        virtual OrderList::Order fetch_order(const std::string_view &id) override;
    protected:
        CoinbaseAdv &_owner;
    };


    std::string api_key;
    std::string api_secret;

    std::optional<double> fee;

    std::string calculate_signature(std::uint64_t timestamp, std::string_view method, std::string_view reqpath, std::string_view body) const;
    json::Value headers(std::string_view method, std::string_view reqpath, std::string_view body) const;

    json::Value GET(std::string_view uri, json::Value query) const;
    json::Value POST(std::string_view uri, json::Value body);

    std::map<std::string, double, std::less<>> balance_cache;
    bool _need_update_balance = true;


    void update_balances();

    void processError(HTTPJson::UnknownStatusException &e) const;

    std::mutex _ws_mx;
    OrderBook _orderbook;
    MyOrderList _orders;
    std::size_t _orders_updates = 0;
    bool _order_changes = false;

    MyWsInstance ws;


    json::Value ws_subscribe(bool unsubscribe, std::vector<std::string_view> products, std::string_view channel);

//    void reject_all_tickers();

    std::size_t _orderCounter;


    json::Value genUniqOrderID(json::Value orderClientID);
    static json::Value parseUniqOrderID(json::Value uniqId);



};


#endif /* SRC_BROKERS_COINBASE_COINBASE_ADVANCED_H_ */
