#pragma once
#ifndef SRC_BROKERS_XTB_STREAMING_H_
#define SRC_BROKERS_XTB_STREAMING_H_


#include "ws.h"
#include "send_block.h"

#include "types.h"

#include <memory>


class XTBStreaming: public std::enable_shared_from_this<XTBStreaming> {
public:

    template<typename DataType>
    using StreamCallback = std::function<void(const std::vector<DataType> &)>;

    XTBStreaming(simpleServer::HttpClient &httpc, std::string stream_url);


    template<typename DataType>
    class Subscription {
    public:

        Subscription(std::shared_ptr<XTBStreaming> hub, std::string symbol, StreamCallback<DataType> cb)
            :_owner(std::move(hub)),_symbol(std::move(symbol)), _cb(std::move(cb)) {}
        ~Subscription();

        ///accepts array of events - however single item can be accepted (as long as it is not array)
        void post_events(const json::Value &data, bool snapshot);
    protected:
        std::weak_ptr<XTBStreaming> _owner;
        std::string _symbol;
        StreamCallback<DataType> _cb;
        std::vector<DataType> _tmp;
        friend class XTBStreaming;
        void push_data(const json::Value &data, bool snapshot);
    };

    using QuoteSubscription = std::shared_ptr<Subscription<Quote> >;
    QuoteSubscription subscribe_quotes(std::string symbol, StreamCallback<Quote> cb);
    using TradeSubscription = std::shared_ptr<Subscription<Position> >;
    TradeSubscription subscribe_trades(StreamCallback<Position> cb);
    using TradeStatusSubscription = std::shared_ptr<Subscription<TradeStatus> >;
    TradeStatusSubscription subscribe_tradeStatus(StreamCallback<TradeStatus> cb);


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
    Lst<TradeStatus> _tradeStatus_submap;
    Lst<TradeStatus> _tradeStatus_tmplst;
    XTBSendBlock<std::chrono::milliseconds> _send_block = {std::chrono::milliseconds(200)};
    std::chrono::system_clock::time_point _ping_expire;
    bool _need_init = true;
    bool _need_reconnect = false;


    void unsubscribe(const std::string &symbol, Subscription<Quote> *ptr);
    void unsubscribe(const std::string &dummy, Subscription<Position> *ptr);
    void unsubscribe(const std::string &dummy, Subscription<TradeStatus> *ptr);
    void init_handler();

    bool data_input(WsInstance::EventType event, json::Value data);

    void subscribe_symbol_quotes(const std::string &symbol);
    void unsubscribe_symbol_quotes(const std::string &symbol);
    void subscribe_trades();
    void unsubscribe_trades();
    void subscribe_tradeStatus();
    void unsubscribe_tradeStatus();
    void reconnect();
    void on_data(json::Value data);
};


#endif /* SRC_BROKERS_XTB_STREAMING_H_ */
