#pragma once
#ifndef SRC_BROKERS_WS_SUPPORT_H_
#define SRC_BROKERS_WS_SUPPORT_H_

#include <imtjson/value.h>
#include <simpleServer/http_client.h>
#include <simpleServer/websockets_stream.h>

#include <future>
#include <memory>
#include <thread>

class WsInstance {
public:

    enum class EventType {
        data,       //<data arrived
        exception,      //<exception arrived
        connect,      //<connect happen - you probably need to resubscribe
        disconnect,     //<disconnect happen - probably to fail waitings
    };

    class Handler: public json::RefCntObj {
    public:
        virtual bool run(EventType event, const json::Value &data) = 0;
        virtual ~Handler() = default;
    };

    template<typename Fn>
    class FnHandler: public Handler {
    public:
        FnHandler(Fn &&fn):_fn(std::move(fn)) {}
        virtual bool run(EventType event, const json::Value &data) {
            return _fn(event, data);
        }

    protected:
        Fn _fn;
    };


    using PHandler = json::RefCntPtr<Handler>;

    WsInstance(simpleServer::HttpClient &client, std::string wsurl);
    WsInstance(const WsInstance &other) = delete;
    WsInstance &operator=(const WsInstance &other) = delete;
    virtual ~WsInstance();


    virtual json::Value generate_headers() {return json::Value();};
    virtual void on_ping();
    void on_connect(std::unique_lock<std::recursive_mutex> &lk);
    void on_disconnect(std::unique_lock<std::recursive_mutex> &lk);


    template<typename Fn>
    void regHandler(Fn h) {
        regHandler_2(new FnHandler<Fn>(std::move(h)));
    }
    template<typename Fn>
    void regMonitor(Fn &&h){
        regMonitor_2(new FnHandler<Fn>(std::move(h)));
    }

    void worker(std::promise<std::exception_ptr> *start_p);

    virtual void send(json::Value v);

protected:
    simpleServer::HttpClient &_client;
    std::string _wsurl;
    simpleServer::WebSocketStream _ws;
    std::recursive_mutex _mx;
    std::vector<PHandler> _handlers;
    std::vector<PHandler> _monitors;
    std::vector<PHandler> _tmphandlers;
    std::vector<PHandler> _tmphandlers_erase;
    std::thread _thr;
    bool _running = false;

    virtual void broadcast(EventType event, const json::Value &data, std::unique_lock<std::recursive_mutex> &lk);
    void process_message(std::string_view msg, std::unique_lock<std::recursive_mutex> &lk);
    void ensure_start(std::unique_lock<std::recursive_mutex> &lk);

    void close_lk(std::unique_lock<std::recursive_mutex> &lk);

    void regHandler_2(PHandler &&h);
    void regMonitor_2(PHandler &&h);
};




#endif /* SRC_BROKERS_WS_SUPPORT_H_ */
