#pragma once
#ifndef SRC_BROKERS_XTB_ORDERS_H_
#define SRC_BROKERS_XTB_ORDERS_H_

#include "client.h"

#include "position_control.h"

#include "../../main/istockapi.h"
#include <memory>

class XTBOrderbookEmulator: public std::enable_shared_from_this<XTBOrderbookEmulator> {
public:

    XTBOrderbookEmulator(XTBClient &client, std::shared_ptr<PositionControl> positions);

    json::Value placeOrder(const std::string &symbol, double size, double price,
                        json::Value clientId, json::Value replaceId);


    class Order : public IStockApi::Order {
    public:
        Order() = default;
        Order(IStockApi::Order ord):IStockApi::Order(std::move(ord)) {}
        std::string last_exec_error;
        bool executed = false;
    };

    std::vector<Order> get_orders(const std::string &symbol) const;

    IStockApi::Ticker get_ticker(const std::string &symbol) ;


    static std::shared_ptr<XTBOrderbookEmulator> create(XTBClient &client, std::shared_ptr<PositionControl> positions);
protected:
    struct OrderList {
        std::vector<Order> _list;
        std::chrono::system_clock::time_point _last_exec;
        XTBClient::QuoteSubscription _subs;
        std::weak_ptr<XTBOrderbookEmulator> _owner;
        std::optional<IStockApi::Ticker> _ticker;
    };

    using OrdersPerSymbol = std::unordered_map<std::string, std::unique_ptr<OrderList>  >;
    mutable std::mutex _mx;
    std::condition_variable _ntf;
    XTBClient &_client;
    std::shared_ptr<PositionControl> _positions;
    XTBClient::TradeStatusSubscription _trade_status_subs;
    OrdersPerSymbol _orderbooks;
    unsigned int _order_counter;

    void on_quote(const std::string &symbol, Quote);
    void on_trade_status(const TradeStatus &status);
    OrderList &get_orderbook(std::unique_lock<std::mutex> &lk, const std::string &symbol);
    OrderList *get_orderbook_ptr(const std::string &symbol) const;

    void init_trade_status();


};




#endif /* SRC_BROKERS_XTB_ORDERS_H_ */
