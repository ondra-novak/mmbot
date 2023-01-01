/*
 * orderbook.h
 *
 *  Created on: 18. 12. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_COINBASE_ORDERBOOK_H_
#define SRC_BROKERS_COINBASE_ORDERBOOK_H_

#include "../ws_support.h"
#include "../../main/istockapi.h"

#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <variant>

class OrderBook {
public:

    
    void clear();
    bool is_product_ready(std::string_view name) const;

    
    std::string_view process_event(WsInstance::EventType event, json::Value data);
    
    std::future<bool> init_product(std::string_view name);
    
    bool any_product() const;
    
    std::optional<IStockApi::Ticker> getTicker(std::string_view product);
    void erase_product(std::string_view product);
    
    static constexpr int collect_timeout_sec = 150;
    
protected:
    
    struct OrderBookItem {
        std::map<double, double> _buy, _sell;
        std::optional<std::promise<bool> > _wait;
        std::chrono::system_clock::time_point _expires;
        std::chrono::system_clock::time_point _last_update;
        std::size_t _updates = 0;
        
    };
    
    std::map<std::string, OrderBookItem,std::less<> > _orderBooks;

    
    std::string_view  process_data(json::Value data);
    
};



#endif /* SRC_BROKERS_COINBASE_ORDERBOOK_H_ */
