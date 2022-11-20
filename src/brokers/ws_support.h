/*
 * ws_support.h
 *
 *  Created on: 20. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_WS_SUPPORT_H_
#define SRC_BROKERS_WS_SUPPORT_H_


/*
 * ws_support.cpp
 *
 *  Created on: 20. 11. 2022
 *      Author: ondra
 */


#include <imtjson/value.h>
#include <simpleServer/http_client.h>
#include <simpleServer/websockets_stream.h>

#include <future>
#include <memory>
#include <thread>

class WsInstance {
public:
        
    
    using Handler = std::function<bool(bool ok, json::Value data)>;
    
    WsInstance(simpleServer::HttpClient &client, std::string wsurl);
    WsInstance(const WsInstance &other) = delete;
    WsInstance &operator=(const WsInstance &other) = delete;
    ~WsInstance();

    
    void start(json::Value hdrs);
    
    void regHandler(Handler &&h);

    void worker(std::promise<void> *start_p);
    
    void send(json::Value v);
   
    void lock() {
        _mx.lock();
    }
    
    void unlock() {
        _mx.unlock();
    }
    
    bool try_lock() {
        return _mx.try_lock();
    }
    
protected:
    simpleServer::HttpClient &_client;
    std::string _wsurl;
    simpleServer::WebSocketStream _ws;
    std::mutex _mx;
    std::vector<Handler> _handlers;
    std::thread _thr;
    bool _close = false;
    bool _running = false;
    json::Value _headers;

    void broadcast(bool ok, json::Value data);
    void process_message(std::string_view msg);
    void ensure_start(std::unique_lock<std::mutex> &lk);

    void close_lk(std::unique_lock<std::mutex> &lk);
};




#endif /* SRC_BROKERS_WS_SUPPORT_H_ */
