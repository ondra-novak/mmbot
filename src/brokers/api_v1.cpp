/*
 * api_v1.cpp
 *
 *  Created on: 3. 7. 2022
 *      Author: ondra
 */

#include "api.h"

#include <imtjson/string.h>
#include <imtjson/array.h>
#include <imtjson/object.h>
#include <imtjson/binjson.tcc>
#include <imtjson/binary.h>
#include <imtjson/operations.h>

#include <random>


// emulate function by collecting data by old api
AbstractBrokerAPI::TradingStatus AbstractBrokerAPI::getTradingStatus(const std::string_view &pair, json::Value instance) {
    bool filter_up = true;
    if (!instance.hasValue()) {
        std::random_device dev;
        int mark = static_cast<int>(dev() & 0x7FFFFFFF);
        auto nfo = getMarketInfo(pair);
        instance = json::Object{
            {"order_mark", mark},
            {"a",nfo.asset_symbol},
            {"c",nfo.currency_symbol}
        };
        filter_up = false;
    }
    json::Value mark = instance["order_mark"];

    TradesSync trades = syncTrades(instance["lastId"], pair);
    instance.setItems({{"lastId", trades.lastId}});

    AbstractBrokerAPI::TradingStatus status;
    status.balance = getBalance(instance["c"].getString(), pair);
    status.position = getBalance(instance["a"].getString(), pair);
    status.openOrders = getOpenOrders(pair);
    status.newTrades = std::move(trades.trades);
    status.ticker = getTicker(pair);
    status.instance = instance;

    //new api wants only orders generated by the api, so we have to remove extra ones
    if (filter_up) {
        auto itr = std::remove_if(status.openOrders.begin(), status.openOrders.end(), [&](const Order &ord) {
            return ord.client_id != mark;
        });
        status.openOrders.erase(itr, status.openOrders.end());
    }

    return status;
}

void AbstractBrokerAPI::placeOrders(const std::string_view &pair, std::vector<OrderToPlace> &orders, json::Value &instance) {
    json::Value mark = instance["order_mark"];
    for (OrderToPlace &ord: orders) {
        try {
            placeOrder(pair, ord.size, ord.price,mark,ord.id_replace,ord.size_replace);
            ord.placed = true;
        } catch (std::exception &e) {
            ord.placed = false;
            ord.error = e.what();
        }
    }
}
