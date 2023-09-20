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

    using Result = std::variant<json::Value, Error>;

    static bool isError(const Result &res);
    static bool isResult(const Result &res);
    static const Error getError(const Result &res);
    static const json::Value &getResult(const Result &res);


    using ResultCallback = std::function<void(const Result &result)>;

    class Request {
    public:
        Request (XTBClient &owner,std::string command,json::Value arguments);
        void operator>>(ResultCallback cb);
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


    void login(Credentials c);



protected:

    std::optional<Credentials> _credents;

    XTBWsInstance _wscntr;
    std::shared_ptr<XTBStreaming> _streaming;
    using RequestMap = std::unordered_map<std::string, ResultCallback>;

    std::mutex _mx;
    RequestMap _requests;
    unsigned int _counter = 1;



    bool data_input(WsInstance::EventType event, json::Value data);
    void reject_all(const Error &err);
    void route_result(const json::Value res);

    void send_command(const std::string &command, const json::Value &args, ResultCallback &&cb);
    void on_login(const json::Value &res);
};



#endif /* SRC_BROKERS_XTB_CLIENT_H_ */
