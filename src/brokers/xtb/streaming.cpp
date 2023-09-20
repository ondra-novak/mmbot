#include "streaming.h"
#include <imtjson/object.h>

XTBStreaming::XTBStreaming(simpleServer::HttpClient &httpc, std::string stream_url)
:_wsstream(httpc, stream_url)
{

}

void XTBStreaming::subscribe_symbol(const std::string &symbol) {
    _send_block();
    _wsstream.send_command("getTickPrices", json::Object{
        { "symbol", symbol },
        { "minArrivalTime", 1 },
        { "maxLevel", 0 }
    });
}

XTBStreaming::Subscription XTBStreaming::subscribe(std::string symbol, StreamCallback cb) {
    auto ptr = std::make_unique<XTBStreaming::SubscriptionImpl>(shared_from_this(), symbol, std::move(cb));
    bool do_subs = false;
    {
        std::lock_guard _(_mx);
        init_handler();
        auto &lst = _submap[symbol];
        do_subs = lst.empty();
        lst.push_back(ptr.get());
    }
    if (do_subs) {
        subscribe_symbol(symbol);
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

void XTBStreaming::unsubscribe_symbol(const std::string &symbol) {
    _wsstream.send( json::Object{
        { "command", "stopTickPrices" },
        { "symbol", symbol }
    });
}

void XTBStreaming::unsubscribe(const std::string &symbol, SubscriptionImpl *ptr) {
    bool do_unsub = false;
    {
        std::lock_guard _(_mx);
        auto &lst = _submap[symbol];
        auto itr = std::find(lst.begin(), lst.end(), ptr);
        if (itr == lst.end()) return;
        lst.erase(itr);
        if (lst.empty()) {
            _submap.erase(symbol);
            do_unsub = true;
        }
    }
    if (do_unsub) {
        unsubscribe_symbol(symbol);
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
    for (const auto &[symbol, lst]: _submap) {
        subscribe_symbol(symbol);
    }
}

void XTBStreaming::on_data(json::Value packet) {
    if (packet["command"].getString() == "tickPrices" && packet["level"].getUInt() == 0) {
        json::Value data = packet["data"];
        std::string symbol = data["symbol"].getString();
        std::lock_guard _(_mx);
        auto iter = _submap.find(symbol);
        if (iter == _submap.end()) return;
        _tmplst = iter->second;
        for (auto x: _tmplst) {
            x->_cb(Quote{data["bid"].getNumber(),
                         data["ask"].getNumber(),
                         data["timestamp"].getUIntLong()});
        }
    }
}

void XTBStreaming::Ws::send_command(std::string_view command, json::Value data) {
    data.setItems({{"command",command},{"streamSessionId",_session_id}});
    send(data);

}

XTBStreaming::SubscriptionImpl::~SubscriptionImpl() {
    auto s = _owner.lock();
    s->unsubscribe(_symbol, this);
}
