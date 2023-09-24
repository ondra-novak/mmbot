#include "orders.h"
#include <imtjson/object.h>
XTBOrderbookEmulator::XTBOrderbookEmulator(XTBClient &client, std::shared_ptr<PositionControl> positions)
    :_client(client)
    ,_positions(positions)
    ,_order_counter(1)
{
}

auto get_executor(XTBClient &client, const std::string &comment) {
    return [&](const std::string &symbol, const PositionControl::Cmd &c) {
        XTBClient::Result res = client("tradeTransaction", json::Object{
            {"tradeTransInfo",json::Object{
               {"cmd",static_cast<int>(c.cmd)},
               {"order",c.order2},
               {"price", c.price_hint},
               {"symbol", symbol},
               {"type", static_cast<int>(c.type)},
               {"customComment", comment},
               {"volume", c.volume}
            }}
        });
        if (XTBClient::is_error(res)) {
            const auto &err =XTBClient::get_error(res);
            throw err;
        }
    };
}

json::Value XTBOrderbookEmulator::placeOrder(const std::string &symbol,
        double size, double price, json::Value clientId, json::Value replaceId) {

    std::unique_lock lk(_mx);
    init_trade_status();
    OrderList &orderbook = get_orderbook(lk, symbol);
    if (replaceId.hasValue()) {
        auto iter = std::find_if(orderbook._list.begin(), orderbook._list.end(),
                [&](const Order &order) {
           return order.id == replaceId;
        });
        if (iter == orderbook._list.end()) throw std::runtime_error("Order already executed");
        std::string err = std::move(iter->last_exec_error);
        if (size) {
            iter->client_id = clientId;
            iter->price = price;
            iter->size = size;
        } else {
            orderbook._list.erase(iter);
            return nullptr;
        }
        if (!err.empty()) throw std::runtime_error(err);
        return iter->id;
    } else if (size) {
        Order ord;
        ord.client_id = clientId;
        ord.id = std::to_string(_order_counter);
        ++_order_counter;
        ord.price = price;
        ord.size = size;
        orderbook._list.push_back(ord);
        return ord.id;
    }
    return nullptr;
}

static constexpr std::string_view order_tag = "MMBOT:";


void XTBOrderbookEmulator::on_trade_status(const TradeStatus &status) {
    if (status.status == TradeStatus::PENDING) return;
    std::string_view comment = status.customComment;
    if (comment.substr(0, order_tag.size()) == order_tag) {
        comment = comment.substr(order_tag.size());
        auto np = comment.find('/');
        if (np != comment.npos) {
            std::string symbol ( comment.substr(0,np));
            std::string order (comment.substr(np+1));
            std::unique_lock lk(_mx);
            auto orderbook = get_orderbook_ptr(symbol);
            if (orderbook) {
                auto iter = std::find_if(orderbook->_list.begin(), orderbook->_list.end(),
                        [&](const Order &ord) {
                    return ord.id.getString() == order;
                });
                if (iter != orderbook->_list.end()) {
                    if (status.status == TradeStatus::ACCEPTED) {
                        orderbook->_list.erase(iter);
                    } else {
                        iter->last_exec_error = status.message;
                    }
                }
            }

        }
    }
}

std::vector<IStockApi::Order> XTBOrderbookEmulator::get_orders(const std::string &symbol) const {
    std::vector<IStockApi::Order>  out;
    std::unique_lock lk(_mx);
    OrderList *olist = get_orderbook_ptr(symbol);
    if (olist) {
        std::transform(olist->_list.begin(), olist->_list.end(), std::back_inserter(out), [&](const Order &x){
            return x;
        });
    }
    return out;
}

IStockApi::Ticker XTBOrderbookEmulator::get_ticker(const std::string &symbol) {
    std::unique_lock lk(_mx);
    OrderList &olist = get_orderbook(lk, symbol);
    return olist._ticker;

}

void XTBOrderbookEmulator::init_trade_status() {
    if (_trade_status_subs) return;
    _trade_status_subs = _client.subscribe_tradeStatus([wk = weak_from_this()](const std::vector<TradeStatus> &st){
        if (st.empty()) return;
       auto _this = wk.lock();
       if (_this) {
           for(const auto &s: st) {
               _this->on_trade_status(s);
           }
       }
    });
}

std::shared_ptr<XTBOrderbookEmulator> XTBOrderbookEmulator::create(XTBClient &client, std::shared_ptr<PositionControl> positions) {
    return std::make_shared<XTBOrderbookEmulator>(client, positions);
}


void XTBOrderbookEmulator::on_quote(const std::string &symbol,Quote quote) {
    std::unique_lock lk(_mx);
    OrderList *orderbook = get_orderbook_ptr(symbol);
    if (!orderbook) return;

    std::string comment(order_tag);
    comment.append(symbol);
    comment.append("/");

    for (Order &ord: orderbook->_list) {
        comment.resize(symbol.size()+order_tag.size()+1);
        if (ord.executed) continue;
        try {
            if ((ord.size > 0 && ord.price > quote.ask) || (ord.size < 0 && ord.price < quote.bid)) {
                    comment.append(ord.id.getString());
                    ord.executed = true;
                    _positions->execute_trade(symbol, ord.size, quote.ask, get_executor(_client, comment));
            }
        } catch (const std::exception &e) {
            ord.last_exec_error = e.what();
        }
    }

    orderbook->_ticker.ask = quote.ask;
    orderbook->_ticker.bid = quote.bid;
    orderbook->_ticker.time = quote.timestamp.get_millis();
    orderbook->_ticker.last = std::max(std::min(orderbook->_ticker.last, quote.ask), quote.bid);

}

XTBOrderbookEmulator::OrderList& XTBOrderbookEmulator::get_orderbook(std::unique_lock<std::mutex> &lk, const std::string &symbol) {

    auto iter = _orderbooks.find(symbol);
    if (iter != _orderbooks.end()) return *iter->second;
    auto ol = std::make_unique<OrderList>();
    ol->_owner = weak_from_this();
    ol->_last_exec = std::chrono::system_clock::now();
    OrderList& ret = *ol;
    _orderbooks.emplace(symbol, std::move(ol));
    lk.unlock();
    ret._subs = _client.subscribe_quotes(symbol,
            [wk = weak_from_this(), symbol](const std::vector<Quote> &quote){
        if (!quote.empty())  {
            auto _this = wk.lock();
            if (_this) {
                _this->on_quote(symbol, quote.back());
            }
        }
    });
    lk.lock();
    return ret;

}

XTBOrderbookEmulator::OrderList* XTBOrderbookEmulator::get_orderbook_ptr(
        const std::string &symbol) const {
    auto iter = _orderbooks.find(symbol);
    if (iter != _orderbooks.end()) return iter->second.get();
    else return nullptr;


}
