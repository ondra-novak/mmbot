#pragma once
#ifndef SRC_BROKERS_XTB_WS_H_
#define SRC_BROKERS_XTB_WS_H_

#include "../ws_support.h"


class XTBWsInstance: public WsInstance {
public:

    using WsInstance::WsInstance;

    using Logger = std::function<void(bool outbound, EventType type, const json::Value &data)>;

    virtual void on_ping() override {
        WsInstance::on_ping();
        auto now = std::chrono::system_clock::now();
        if (now > _ping_expire) {
            send_command("ping", json::object);
            update_interval();
        }
    }

    virtual void send_command(std::string_view command, json::Value data) {
        data.setItems({{"command",command}});
        send(data);
    }

    void set_logger(Logger logger) {
        std::lock_guard _(_mx);
        _logger = std::move(logger);
    }

    virtual void send(json::Value v) override {
        if (_logger) _logger(true, EventType::data, v);
        std::lock_guard _(_mx);
        std::this_thread::sleep_until(_block_expire);
        WsInstance::send(std::move(v));
        _block_expire = std::chrono::system_clock::now()+std::chrono::milliseconds(220);
    }

protected:
    Logger _logger;
    std::chrono::system_clock::time_point _ping_expire;
    std::chrono::system_clock::time_point _block_expire;

    virtual void broadcast(WsInstance::EventType event, const json::Value &data) override {
        if (event == EventType::connect) update_interval();
        if (_logger) _logger(false, event, data);
        WsInstance::broadcast(event, std::move(data));
    }

    void update_interval() {
        _ping_expire = std::chrono::system_clock::now()+ std::chrono::minutes(5);
    }
};


#endif /* SRC_BROKERS_XTB_WS_H_ */
