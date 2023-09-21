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
}

XTBStreaming::TradeSubscription XTBStreaming::subscribe_trades( StreamCallback<Trade> cb) {
    auto ptr = std::make_shared<XTBStreaming::Subscription<Trade> >(shared_from_this(), "", std::move(cb));
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

void XTBStreaming::unsubscribe(const std::string &dummy, Subscription<Trade> *ptr) {
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

template<>
void XTBStreaming::Subscription<XTBStreaming::Quote>::post_data(const json::Value &data) {
    _cb(Quote{data["bid"].getNumber(),data["ask"].getNumber(),data["timestamp"].getUIntLong()});
}
template<>
void XTBStreaming::Subscription<XTBStreaming::Trade>::post_data(const json::Value &data) {
    _cb(Trade{});
}


void XTBStreaming::on_data(json::Value packet) {
    if (packet["command"].getString() == "tickPrices") {
        json::Value data = packet["data"];
        std::string symbol = data["symbol"].getString();
        std::lock_guard _(_mx);
        auto iter = _quote_submap.find(symbol);
        if (iter == _quote_submap.end()) return;
        _quote_tmplst = iter->second;
        for (auto x: _quote_tmplst) {
            x->post_data(data);
        }
    }
    if (packet["command"].getString() == "trade") {
        json::Value data = packet["data"];
        std::lock_guard _(_mx);
        _trade_tmplst = _trade_submap;
        for (auto x: _trade_tmplst) {
            x->post_data(data);
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
