#pragma once
#ifndef SRC_BROKERS_XTB_STREAMING_H_
#define SRC_BROKERS_XTB_STREAMING_H_


#include "ws.h"
#include "send_block.h"
#include <memory>


class XTBStreaming: public std::enable_shared_from_this<XTBStreaming> {
public:

    struct Quote {
        double bid;
        double ask;
        std::uint64_t timestamp;
    };

    using StreamCallback = std::function<void(const Quote &)>;

    XTBStreaming(simpleServer::HttpClient &httpc, std::string stream_url);


    class SubscriptionImpl {
    public:

        SubscriptionImpl(std::shared_ptr<XTBStreaming> hub, std::string symbol, StreamCallback cb)
            :_owner(std::move(hub)),_symbol(std::move(symbol)), _cb(std::move(cb)) {}
        ~SubscriptionImpl();
    protected:
        std::weak_ptr<XTBStreaming> _owner;
        std::string _symbol;
        StreamCallback _cb;
        friend class XTBStreaming;
    };




    using Subscription = std::unique_ptr<SubscriptionImpl>;

    Subscription subscribe(std::string symbol, StreamCallback cb);


    void set_session_id(std::string session_id);


    void set_logger(XTBWsInstance::Logger log) {
        _wsstream.set_logger(log);
    }
protected:

    class Ws: public XTBWsInstance {
    public:
        using XTBWsInstance::XTBWsInstance;
        virtual void send_command(std::string_view command, json::Value data) override;
        std::string _session_id;
    };

    Ws _wsstream;

    using Lst = std::vector<SubscriptionImpl *> ;
    using SubMap = std::unordered_map<std::string, Lst>;

    std::recursive_mutex _mx;
    SubMap _submap;
    Lst _tmplst;
    XTBSendBlock<std::chrono::milliseconds> _send_block = {std::chrono::milliseconds(200)};
    std::chrono::system_clock::time_point _ping_expire;
    bool _need_init = true;
    bool _need_reconnect = false;


    void unsubscribe(const std::string &symbol, SubscriptionImpl *ptr);
    void init_handler();

    bool data_input(WsInstance::EventType event, json::Value data);

    void subscribe_symbol(const std::string &symbol);
    void reconnect();
    void on_data(json::Value data);
    void unsubscribe_symbol(const std::string &symbol);
};




#endif /* SRC_BROKERS_XTB_STREAMING_H_ */
