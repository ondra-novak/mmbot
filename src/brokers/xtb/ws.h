#pragma once
#ifndef SRC_BROKERS_XTB_WS_H_
#define SRC_BROKERS_XTB_WS_H_

#include "../ws_support.h"


class XTBWsInstance: public WsInstance {
public:

    using WsInstance::WsInstance;

    virtual void on_ping() override {
        WsInstance::on_ping();
        auto now = std::chrono::system_clock::now();
        if (now > _ping_expire) {
            send_command("ping", json::object);
        }
        update_interval();
    }

    virtual void on_connect() override {
        WsInstance::on_connect();
        update_interval();
    }

    virtual void send_command(std::string_view command, json::Value data) {
        data.setItems({{"command",command}});
        send(data);
    }

protected:
    std::chrono::system_clock::time_point _ping_expire;
    void update_interval() {
        _ping_expire = std::chrono::system_clock::now()+ std::chrono::minutes(5);
    }
};


#endif /* SRC_BROKERS_XTB_WS_H_ */
