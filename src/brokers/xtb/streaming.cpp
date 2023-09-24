#include "streaming.h"
#include <imtjson/object.h>



XTBStreaming::XTBStreaming(simpleServer::HttpClient &httpc, std::string stream_url)
:_wsstream(httpc, stream_url)
{

}

void XTBStreaming::subscribe_symbol_quotes(const std::string &symbol) {
    _send_block();
    _wsstream.send_command("getTickPrices", json::Object{
        { "symbol", symbol },
        { "minArrivalTime", 1 },
        { "maxLevel", 0 }
    });
}
void XTBStreaming::unsubscribe_symbol_quotes(const std::string &symbol) {
    _wsstream.send( json::Object{
        { "command", "stopTickPrices" },
        { "symbol", symbol }
    });
}

void XTBStreaming::subscribe_trades() {
    _send_block();
    _wsstream.send_command("getTrades", json::object);
}
void XTBStreaming::unsubscribe_trades() {
    _wsstream.send( json::Object{
        { "command", "stopTrades" },
    });
}
void XTBStreaming::subscribe_tradeStatus() {
    _send_block();
    _wsstream.send_command("getTradeStatus", json::object);
}
void XTBStreaming::unsubscribe_tradeStatus() {
    _wsstream.send( json::Object{
        { "command", "stopTradeStatus" },
    });
}


XTBStreaming::QuoteSubscription XTBStreaming::subscribe_quotes(std::string symbol, StreamCallback<Quote> cb) {
    auto ptr = std::make_shared<XTBStreaming::Subscription<Quote> >(shared_from_this(), symbol, std::move(cb));
    bool do_subs = false;
    {
        std::lock_guard _(_mx);
        init_handler();
        auto &lst = _quote_submap[symbol];
        do_subs = lst.empty();
        lst.push_back(ptr.get());
    }
    if (do_subs) {
        subscribe_symbol_quotes(symbol);
    }
    return ptr;
}

void XTBStreaming::set_session_id(std::string session_id) {
    std::lock_guard _(_mx);
    _wsstream._session_id = session_id;
}

void XTBStreaming::init_handler() {
    if (_need_init) {
        _wsstream.regHandler([this](WsInstance::EventType event, json::Value data){
            return data_input(event, data);
        });
        _need_init = false;
    }
}

void XTBStreaming::unsubscribe(const std::string &symbol, Subscription<Quote> *ptr) {
    bool do_unsub = false;
    {
        std::lock_guard _(_mx);
        auto &lst = _quote_submap[symbol];
        auto itr = std::find(lst.begin(), lst.end(), ptr);
        if (itr == lst.end()) return;
        lst.erase(itr);
        if (lst.empty()) {
            _quote_submap.erase(symbol);
            do_unsub = true;
        }
    }
    if (do_unsub) {
        unsubscribe_symbol_quotes(symbol);
    }
}

bool XTBStreaming::data_input(WsInstance::EventType event, json::Value data) {
    switch (event) {
        case WsInstance::EventType::connect:
            if (_need_reconnect) reconnect();
            break;
        case WsInstance::EventType::exception:
        case WsInstance::EventType::disconnect:
            _need_reconnect = true;
            break;
        case WsInstance::EventType::data:
            on_data(data);
            break;
    }
    return true;
}

void XTBStreaming::reconnect() {
    std::lock_guard _(_mx);
    _ping_expire = std::chrono::system_clock::now()+std::chrono::minutes(6);
    for (const auto &[symbol, lst]: _quote_submap) {
        subscribe_symbol_quotes(symbol);
    }
    if (!_trade_submap.empty()) subscribe_trades();
    if (!_tradeStatus_submap.empty()) subscribe_tradeStatus();
}

XTBStreaming::TradeSubscription XTBStreaming::subscribe_trades( StreamCallback<Position> cb) {
    auto ptr = std::make_shared<XTBStreaming::Subscription<Position> >(shared_from_this(), "", std::move(cb));
    bool do_subs = false;
    {
        std::lock_guard _(_mx);
        do_subs = _trade_submap.empty();
        _trade_submap.push_back(ptr.get());
        init_handler();
    }
    if (do_subs) {
        subscribe_trades();
    }
    return ptr;
}

XTBStreaming::TradeStatusSubscription XTBStreaming::subscribe_tradeStatus( StreamCallback<TradeStatus> cb) {
    auto ptr = std::make_shared<XTBStreaming::Subscription<TradeStatus> >(shared_from_this(), "", std::move(cb));
    bool do_subs = false;
    {
        std::lock_guard _(_mx);
        do_subs = _tradeStatus_submap.empty();
        _tradeStatus_submap.push_back(ptr.get());
        init_handler();
    }
    if (do_subs) {
        subscribe_tradeStatus();
    }
    return ptr;
}

void XTBStreaming::unsubscribe(const std::string &dummy, Subscription<Position> *ptr) {
    bool do_unsub = false;
    {
        std::lock_guard _(_mx);
        auto &lst = _trade_submap;
        auto itr = std::find(lst.begin(), lst.end(), ptr);
        if (itr == lst.end()) return;
        lst.erase(itr);
        if (lst.empty()) {
            do_unsub = true;
        }
    }
    if (do_unsub) {
        unsubscribe_trades();
    }
}

void XTBStreaming::unsubscribe(const std::string &dummy, Subscription<TradeStatus> *ptr) {
    bool do_unsub = false;
    {
        std::lock_guard _(_mx);
        auto &lst = _tradeStatus_submap;
        auto itr = std::find(lst.begin(), lst.end(), ptr);
        if (itr == lst.end()) return;
        lst.erase(itr);
        if (lst.empty()) {
            do_unsub = true;
        }
    }
    if (do_unsub) {
        unsubscribe_tradeStatus();
    }
}

template<typename DataType>
void XTBStreaming::Subscription<DataType>::post_events(const json::Value &data, bool snapshot) {
    _tmp.clear();
    if (data.type() == json::array) {
        _tmp.reserve(data.size());
        for (json::Value v: data) {
            push_data(v, snapshot);
        }
    } else {
        _tmp.reserve(1);
        push_data(data, snapshot);
    }
    _cb(_tmp);
}


template<>
void XTBStreaming::Subscription<Quote>::push_data(const json::Value &data, bool snapshot) {
    _tmp.push_back(Quote{data["bid"].getNumber(),data["ask"].getNumber(),data["timestamp"].getUIntLong(), snapshot});
}
template<>
void XTBStreaming::Subscription<Position>::push_data(const json::Value &data, bool snapshot) {
    _tmp.push_back(Position::fromJSON(data, snapshot));
}
template<>
void XTBStreaming::Subscription<TradeStatus>::push_data(const json::Value &data, bool snapshot) {
    _tmp.push_back({
        static_cast<TradeStatus::Status>(data["requestStatus"].getUInt()),
        data["price"].getNumber(),
        data["message"].getString(),
        data["customComment"].getString(),
        data["order"].getUIntLong()
    });
}


void XTBStreaming::on_data(json::Value packet) {
    std::string_view command = packet["command"].getString();
    if (command == "tickPrices") {
        json::Value data = packet["data"];
        std::string symbol = data["symbol"].getString();
        std::lock_guard _(_mx);
        auto iter = _quote_submap.find(symbol);
        if (iter == _quote_submap.end()) return;
        _quote_tmplst = iter->second;
        for (auto x: _quote_tmplst) {
            x->post_events(data, false);
        }
    } else if (command == "trade") {
        json::Value data = packet["data"];
        std::lock_guard _(_mx);
        _trade_tmplst = _trade_submap;
        for (auto x: _trade_tmplst) {
            x->post_events(data, false);
        }
    } else if (command == "tradeStatus") {
        json::Value data = packet["data"];
        std::lock_guard _(_mx);
        _tradeStatus_tmplst = _tradeStatus_submap;
        for (auto x: _tradeStatus_tmplst) {
            x->post_events(data, false);
        }
    }
}

void XTBStreaming::Ws::send_command(std::string_view command, json::Value data) {
    data.setItems({{"command",command},{"streamSessionId",_session_id}});
    send(data);

}



template<typename DataType>
XTBStreaming::Subscription<DataType>::~Subscription() {
    auto s = _owner.lock();
    s->unsubscribe(_symbol, this);
}
