#include "orderbook.h"


void OrderBook::clear() {
    for (auto &[id, ob]: _orderBooks) {
        ob._buy.clear();
        ob._sell.clear();
        if (ob._wait.has_value()) {
            ob._wait->set_value(false);            
        }        
    }    
    _orderBooks.clear();
}


bool OrderBook::is_product_ready(std::string_view name) const {
    auto iter = _orderBooks.find(name);
    if (iter == _orderBooks.end()) return false;
    return !iter->second._wait.has_value();
}

std::string_view  OrderBook::process_event(WsInstance::EventType event, json::Value data) {
    switch (event) {
        case WsInstance::EventType::data: return process_data(data);break;
        case WsInstance::EventType::exception:
        case WsInstance::EventType::disconnect: clear();break;
        default:break;
    }
    return {};
}


std::optional<IStockApi::Ticker> OrderBook::getTicker(std::string_view product) {
    auto iter = _orderBooks.find(product);
    if (iter == _orderBooks.end()) return {};
    if (iter->second._wait.has_value()) return {};
    OrderBookItem &o = iter->second;
    if (o._buy.empty() || o._sell.empty()) return {};
    double bid = o._buy.rbegin()->first;
    double ask = o._sell.begin()->first;
    double mid = std::sqrt(bid*ask);
    std::uint64_t tm = std::chrono::duration_cast<std::chrono::milliseconds>(
            o._last_update.time_since_epoch()
    ).count();   
    o._expires = std::chrono::system_clock::now()+std::chrono::seconds(collect_timeout_sec);
    return IStockApi::Ticker {bid,ask,mid,tm};    
}

void OrderBook::erase_product(std::string_view product) {
    auto iter = _orderBooks.find(product);
    if (iter == _orderBooks.end()) return;
    if (iter->second._wait.has_value()) {
        iter->second._wait->set_value(false);                
    }
    _orderBooks.erase(iter);
}

std::string_view OrderBook::process_data(json::Value data) {

  std::string_view unsub;
  if (data["channel"].getString() != "l2_data") return unsub;
  for (json::Value event: data["events"]) {
    std::string_view type = event["type"].getString();
    if (type == "update" || type == "snapshot") {
        auto now = std::chrono::system_clock::now();
        bool snapshot = event["type"].getString() == "snapshot";
        std::string_view product = event["product_id"].getString();
        auto iter = _orderBooks.find(product);
        if (iter == _orderBooks.end()) {
            unsub = product;
            continue;
        }
        if (iter->second._expires < now) {
            _orderBooks.erase(iter);
            unsub = product;
            continue;                                
        }
        
        
        auto &o = iter->second;
        if (snapshot) {
            o._buy.clear();
            o._sell.clear();
        }
        for (json::Value v: event["updates"]) {
            auto &side = v["side"].getString()=="bid"?o._buy:o._sell;
            double q = v["new_quantity"].getNumber();
            double p = v["price_level"].getNumber();
            if (q == 0) side.erase(p);
            else side[p] = q;
        }
        if (o._wait.has_value()) {
            o._wait->set_value(true);
            o._wait.reset();
        }                        
        o._last_update = now;
    }
  }
  return unsub;
}

bool OrderBook::any_product() const {
    return !_orderBooks.empty();
}

std::future<bool> OrderBook::init_product(std::string_view name) {
    auto iter = _orderBooks.find(name);
    if (iter != _orderBooks.end()) {
        if (iter->second._wait.has_value()) {
            return iter->second._wait->get_future();
        } else {
            std::promise<bool> resolved;
            resolved.set_value(true);
            return resolved.get_future();
        }
    }
    OrderBookItem &item = _orderBooks[std::string(name)];
    item._wait.emplace();
    item._expires = std::chrono::system_clock::now()+std::chrono::seconds(collect_timeout_sec);
    return item._wait->get_future();
}

