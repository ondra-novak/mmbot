#include "orderlist.h"
#include "../../main/sgn.h"
#include <shared/logOutput.h>
#include <imtjson/string.h>

using ondra_shared::logDebug;

void OrderList::add(Order order) {
    remove(order.client_id.getString());
    std::size_t index = _orders.size();
    _index.emplace(order.client_id.getString(), index);
    _orders.push_back(std::move(order));
}

void OrderList::remove(const std::string_view id) {
    auto iter = _index.find(id);
    if (iter == _index.end()) return;
    std::size_t last_index = _orders.size()-1;
    std::size_t this_index = iter->second;
    _index.erase(iter);
    if (this_index != last_index) {
        std::swap(_orders[iter->second], _orders[last_index]);
        _index[_orders[this_index].client_id.getString()] = this_index;
    }
    _orders.pop_back();
}

bool OrderList::process_events(WsInstance::EventType event, json::Value data) {
    switch (event) {
        case WsInstance::EventType::data:
            return process_data(data);
        case WsInstance::EventType::exception:
        case WsInstance::EventType::disconnect:
            clear();
            logDebug("WS Event orders disconnected!");
            _orders_ready = false;
            if (_orders_wait.has_value()) {
                _orders_wait->set_value(false);
                _orders_wait.reset();
            }
            return false;
        default:
            return true;
    }
}

void OrderList::clear() {
    _orders.clear();
    _index.clear();

}

bool OrderList::process_data(json::Value data) {
    if (data["channel"].getString() != "user") return true;
    ++_updates;
    for (json::Value event: data["events"]) {
      std::string_view type = event["type"].getString();
      if (type == "update" || type == "snapshot") {
//          auto now = std::chrono::system_clock::now();
          bool snapshot = event["type"].getString() == "snapshot";
          if (snapshot) {
              clear();
          }
          for (json::Value v: event["orders"]) {
              json::Value id = v["order_id"].stripKey();
              std::string_view client_id = v["client_order_id"].getString();
              std::string_view status = v["status"].getString();
              if (status == "OPEN" || status == "PENDING") {
                  logDebug("WS Event: Order updated $1", v.toString().str());
                  auto update_fn = [&](Order &o) {
                      o.size = sgn(o.size)*v["leaves_quantity"].getNumber();
                      o.id = id;
                  };
                  if (!update(client_id, update_fn)) {
                      try {
                          Order x = fetch_order(id.getString());
                          update_fn(x);
                          add(std::move(x));
                      } catch (...) {
                          if (_orders_wait.has_value()) {
                              _orders_ready = true;
                              _orders_wait->set_value(false);
                              _orders_wait.reset();
                          }
                          clear();
                          _orders_ready = false;
                          return false;
                      }
                  }
              } else {
                  logDebug("WS Event: Order removed $1", v.toString().str());
                  remove(client_id);
              }
          }
          if (_orders_wait.has_value()) {
              _orders_ready = true;
              _orders_wait->set_value(true);
              _orders_wait.reset();
          }
      }
    }
    return true;


}

bool OrderList::is_ready() const {
    return _orders_ready;
}

std::future<bool> OrderList::get_ready() {
    if (_orders_ready) {
        std::promise<bool> p;
        p.set_value(true);
        return p.get_future();
    }
    _orders_wait.emplace();
    return _orders_wait->get_future();
}
