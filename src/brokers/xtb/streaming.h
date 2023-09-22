#pragma once
#ifndef SRC_BROKERS_XTB_STREAMING_H_
#define SRC_BROKERS_XTB_STREAMING_H_


#include "ws.h"
#include "send_block.h"

#include "types.h"

#include <memory>


class XTBStreaming: public std::enable_shared_from_this<XTBStreaming> {
public:

    struct Quote {
        double bid;
        double ask;
        std::uint64_t timestamp;
        bool snapshot;
    };

    template<typename DataType>
    using StreamCallback = std::function<void(const DataType &)>;

    XTBStreaming(simpleServer::HttpClient &httpc, std::string stream_url);


    template<typename DataType>
    class Subscription {
    public:

        Subscription(std::shared_ptr<XTBStreaming> hub, std::string symbol, StreamCallback<DataType> cb)
            :_owner(std::move(hub)),_symbol(std::move(symbol)), _cb(std::move(cb)) {}
        ~Subscription();

        void post_data(const json::Value &data, bool snapshot);
    protected:
        std::weak_ptr<XTBStreaming> _owner;
        std::string _symbol;
        StreamCallback<DataType> _cb;
        friend class XTBStreaming;
    };

    using QuoteSubscription = std::shared_ptr<Subscription<Quote> >;
    QuoteSubscription subscribe_quotes(std::string symbol, StreamCallback<Quote> cb);
    using TradeSubscription = std::shared_ptr<Subscription<Position> >;
    TradeSubscription subscribe_trades(StreamCallback<Position> cb);


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


    template<typename DataType>
    using Lst = std::vector<Subscription<DataType> *> ;
    template<typename DataType>
    using SubMap = std::unordered_map<std::string, Lst<DataType> >;

    std::recursive_mutex _mx;
    SubMap<Quote> _quote_submap;
    Lst<Quote> _quote_tmplst;
    Lst<Position> _trade_submap;
    Lst<Position> _trade_tmplst;
    XTBSendBlock<std::chrono::milliseconds> _send_block = {std::chrono::milliseconds(200)};
    std::chrono::system_clock::time_point _ping_expire;
    bool _need_init = true;
    bool _need_reconnect = false;


    void unsubscribe(const std::string &symbol, Subscription<Quote> *ptr);
    void unsubscribe(const std::string &dummy, Subscription<Position> *ptr);
    void init_handler();

    bool data_input(WsInstance::EventType event, json::Value data);

    void subscribe_symbol_quotes(const std::string &symbol);
    void unsubscribe_symbol_quotes(const std::string &symbol);
    void subscribe_trades();
    void unsubscribe_trades();
    void reconnect();
    void on_data(json::Value data);
};


#endif /* SRC_BROKERS_XTB_STREAMING_H_ */
