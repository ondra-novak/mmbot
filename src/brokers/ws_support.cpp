
#include "ws_support.h"

#include <imtjson/string.h>

WsInstance::WsInstance(simpleServer::HttpClient &client, std::string wsurl)
:_client(client), _wsurl(wsurl)
{

}

void WsInstance::regHandler_2(PHandler &&h) {
    std::unique_lock lk(_mx);
    _handlers.push_back(std::move(h));
    try {
        ensure_start(lk);
    } catch (...) {
        _handlers.pop_back();
        throw;
    }
}
void WsInstance::regMonitor_2(PHandler &&h) {
    std::unique_lock lk(_mx);
    _monitors.push_back(std::move(h));
}

void process_message(std::string_view msg);


void WsInstance::on_ping() {
    _ws->ping({});
}

void WsInstance::on_connect(std::unique_lock<std::recursive_mutex> &lk) {
    broadcast(EventType::connect, json::Value(), lk);

}
void WsInstance::on_disconnect(std::unique_lock<std::recursive_mutex> &lk) {
    broadcast(EventType::disconnect, json::Value(), lk);
}

void WsInstance::worker(std::promise<std::exception_ptr> *start_p) {
    using simpleServer::WSFrameType;

    std::unique_lock lk(_mx);
    auto next_reconnect = std::chrono::system_clock::now();
    while (_running) {

        try {
            simpleServer::SendHeaders hdrs;
            for (json::Value x: generate_headers()) {
                hdrs(x.getKey(), x.getString());
            }
            next_reconnect = std::chrono::system_clock::now() + std::chrono::seconds(5);
            _ws = simpleServer::connectWebSocket(_client, _wsurl, std::move(hdrs));
            _ws->getStream()->setIOTimeout(15000);
            if (start_p) {
                start_p->set_value(nullptr);
                start_p = nullptr;
            }
            on_connect(lk);
            bool cont = true;
            bool pinged = false;
            lk.unlock();
            while (_running && cont) {
                cont = false;
                bool cycle = true;
                try {
                    while (_running && cycle &&_ws->read()) {
                        std::unique_lock lk2(_mx);
                        switch (_ws.getFrameType()) {
                        default:
                        case WSFrameType::binary: break;
                        case WSFrameType::incomplete:break;
                        case WSFrameType::connClose:
                            cycle = false;
                        break;
                        case WSFrameType::text: process_message(_ws.getText(), lk2);
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
                            cont = true;
                            continue;
                        }
                    }
                }
            }
            pinged = false;
            lk.lock();

            on_disconnect(lk);
        } catch (std::exception &e) {
            _ws = nullptr;
            if (start_p) {
                start_p->set_value(std::current_exception());
                _running = false;
                return;
            }
            if (!lk.owns_lock()) {
                lk.lock();
            }
            broadcast(EventType::exception, e.what(), lk);
        }
        _ws = nullptr;
        lk.unlock();
        std::this_thread::sleep_until(next_reconnect);
        lk.lock();
    }
}


void WsInstance::close_lk(std::unique_lock<std::recursive_mutex> &lk) {
    if (_running) {
       _running = false;
       if (_ws) _ws->close();
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
    if (lk.owns_lock()) {
        if (_ws) {
            _ws->postText(v.stringify().str());
        } else {
            throw std::runtime_error("Send failed - connection lost");
        }
    }
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

void WsInstance::broadcast(EventType ok, const json::Value &data, std::unique_lock<std::recursive_mutex> &lk) {
    _tmphandlers.clear();
    _tmphandlers_erase.clear();
    for (const auto &x: _monitors) _tmphandlers.push_back(x);
    for (const auto &x: _handlers) _tmphandlers.push_back(x);
    lk.unlock();
    for (auto &x: _tmphandlers) {
        if (!(x->run(ok, data))) _tmphandlers_erase.push_back(x);
    }
    lk.lock();
    if (!_tmphandlers_erase.empty()) {
        for (const auto &x: _tmphandlers_erase) {
            auto iter = std::remove(_monitors.begin(), _monitors.end(), x);
            _monitors.erase(iter, _monitors.end());
            iter = std::remove(_handlers.begin(), _handlers.end(), x);
            _handlers.erase(iter, _handlers.end());
        }
    }
}

void WsInstance::process_message(std::string_view msg, std::unique_lock<std::recursive_mutex> &lk) {
    json::Value v = json::Value::fromString(msg);
    broadcast(EventType::data, v, lk);
}
