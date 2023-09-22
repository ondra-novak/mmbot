#ifndef SRC_BROKERS_XTB_CLIENT_H_
#define SRC_BROKERS_XTB_CLIENT_H_


#include "ws.h"

#include "streaming.h"

#include <memory>
#include <optional>
#include <variant>
#include <unordered_map>

class XTBClient {
public:

    XTBClient(simpleServer::HttpClient &httpc, std::string control_url, std::string streaming_url);

    struct Error {
        std::string code;
        std::string description;
    };

    static const Error error_disconnect;
    static const Error error_exception;

    enum class LogEventType {
        command,
        result,
        stream_request,
        stream_data
    };

    using Result = std::variant<json::Value, Error>;
    using Logger = std::function<void(LogEventType, WsInstance::EventType, const json::Value &data)>;

    static bool is_error(const Result &res);
    static bool is_result(const Result &res);
    static const Error get_error(const Result &res);
    static const json::Value &get_result(const Result &res);


    using ResultCallback = std::function<void(const Result &result)>;

    class Request {
    public:
        Request (XTBClient &owner,std::string command,json::Value arguments);
        void operator>>(ResultCallback cb);
        operator Result() const;
    protected:
        XTBClient &owner;
        std::string command;
        json::Value arguments;
    };

    Request operator()(std::string command, const json::Value &args);

    struct Credentials {
        std::string userId;
        std::string password;
        std::string appName;
        ResultCallback loginCB;
        json::Value toJson() const;
    };


    bool login(Credentials c, bool sync);

    void set_logger(Logger logger);


    XTBStreaming &get_streaming() const {return *_streaming;}

    using Quote = XTBStreaming::Quote;
    using QuoteSubscription = XTBStreaming::QuoteSubscription;
    using TradeSubscription = XTBStreaming::TradeSubscription;
    QuoteSubscription subscribe_quotes(std::string symbol, XTBStreaming::StreamCallback<Quote> cb);
    TradeSubscription subscribe_trades(XTBStreaming::StreamCallback<Position> cb);



protected:
    using RequestMap = std::unordered_map<std::string, ResultCallback>;


    std::mutex _mx;
    RequestMap _requests;
    XTBWsInstance _wscntr;
    std::optional<Credentials> _credents;
    std::shared_ptr<XTBStreaming> _streaming;
    unsigned int _counter = 1;


    bool data_input(WsInstance::EventType event, json::Value data);
    void reject_all(const Error &err);
    void route_result(const json::Value res);

    void send_command(const std::string &command, const json::Value &args, ResultCallback &&cb);
    void on_login(const json::Value &res);
};



#endif /* SRC_BROKERS_XTB_CLIENT_H_ */
