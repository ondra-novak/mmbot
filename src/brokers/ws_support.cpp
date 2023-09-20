
#include "ws_support.h"

#include <imtjson/string.h>

WsInstance::WsInstance(simpleServer::HttpClient &client, std::string wsurl)
:_client(client), _wsurl(wsurl)
{

}

void WsInstance::regHandler(Handler &&h) {
    std::unique_lock lk(_mx);
    _handlers.push_back(std::move(h));
    try {
        ensure_start(lk);
    } catch (...) {
        _handlers.pop_back();
        throw;
    }
}
void WsInstance::regMonitor(Handler &&h) {
    std::unique_lock lk(_mx);
    _monitors.push_back(std::move(h));
}

void process_message(std::string_view msg);


void WsInstance::on_ping() {
    _ws->ping({});
}

void WsInstance::on_connect() {
    broadcast(EventType::connect, json::Value());

}
void WsInstance::on_disconnect() {
    broadcast(EventType::disconnect, json::Value());
}


void WsInstance::worker(std::promise<std::exception_ptr> *start_p) {
    using simpleServer::WSFrameType;

    std::unique_lock lk(_mx);
    while (_running) {

        try {
            simpleServer::SendHeaders hdrs;
            for (json::Value x: generate_headers()) {
                hdrs(x.getKey(), x.getString());
            }
            _ws = simpleServer::connectWebSocket(_client, _wsurl, std::move(hdrs));
            _ws->getStream()->setIOTimeout(15000);
            if (start_p) {
                start_p->set_value(nullptr);
                start_p = nullptr;
            }
            on_connect();
            bool cont = true;
            bool pinged = false;
            lk.unlock();
            while (_running && cont) {
                try {
                    while (_running && cont && _ws->read()) {
                        std::unique_lock lk2(_mx);
                        switch (_ws.getFrameType()) {
                        default:
                        case WSFrameType::binary: break;
                        case WSFrameType::incomplete:break;
                        case WSFrameType::connClose:
                            cont = false;
                        break;
                        case WSFrameType::text: process_message(_ws.getText());
                                                break;
                        }
                        pinged = false;
                    }
                } catch (simpleServer::TimeoutException &e) {
                    if (_handlers.empty()) {
                        _running = false;
                        _thr.detach();
                    } else {
                        if (pinged) {
                            cont = false;
                        } else {
                            pinged = true;
                            on_ping();
                            continue;
                        }
                    }
                }
            }
            pinged = false;
            lk.lock();

            on_disconnect();
        } catch (std::exception &e) {
            _ws = nullptr;
            if (start_p) {
                start_p->set_value(std::current_exception());
                _running = false;
                return;
            }
            broadcast(EventType::exception, e.what());
            if (lk.owns_lock()) {
                lk.unlock(); //unlock temporalily
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
            lk.lock();
        }


    }
}


void WsInstance::close_lk(std::unique_lock<std::recursive_mutex> &lk) {
    if (_running) {
       _running = false;
       _ws->close();
       lk.unlock();
       _thr.join();
    }
}

WsInstance::~WsInstance() {
    std::unique_lock lk(_mx);
    close_lk(lk);
}

void WsInstance::send(json::Value v) {
    std::unique_lock lk(_mx);
    ensure_start(lk);
    _ws->postText(v.stringify().str());
}


void WsInstance::ensure_start(std::unique_lock<std::recursive_mutex> &lk) {
    if (!_running) {
        _running= true;
        std::promise<std::exception_ptr> p;
        _thr = std::thread([this,&p]{
            worker(&p);
        });
        auto f = p.get_future();
        lk.unlock();
        std::exception_ptr e = f.get();
        if (e) {
            _thr.join();
            std::rethrow_exception(e);
        }
    }
}

void WsInstance::broadcast(EventType ok, json::Value data) {
    _monitors.erase(std::remove_if(_monitors.begin(), _monitors.end(), [&](const Handler &h){
        return !h(ok, data);
    }),_monitors.end());
    _handlers.erase(std::remove_if(_handlers.begin(), _handlers.end(), [&](const Handler &h){
        return !h(ok, data);
    }),_handlers.end());
}

void WsInstance::process_message(std::string_view msg) {
    json::Value v = json::Value::fromString(msg);
    broadcast(EventType::data, v);
}
