#include "test.h"
#include "client.h"
#include "position_control.h"
#include <simpleServer/http_client.h>


int main(int, char **) {

    std::mutex console_lock;

    simpleServer::HttpClient httpc("MMBot 2.0 coinbase API client", simpleServer::newHttpsProvider(), nullptr, nullptr);
    XTBClient client(httpc, "wss://ws.xtb.com/demo", "wss://ws.xtb.com/demoStream");
    client.set_logger([&](XTBClient::LogEventType log_ev, WsInstance::EventType ev, const json::Value &v){
        std::lock_guard _(console_lock);
        std::string_view evtype;
        std::string_view action;
        switch (log_ev) {
            case XTBClient::LogEventType::command: evtype = "command";break;
            case XTBClient::LogEventType::result: evtype = "response";break;
            case XTBClient::LogEventType::stream_request: evtype = "stream_request";break;
            case XTBClient::LogEventType::stream_data: evtype = "stream_data";break;
            default: break;
        }
        switch(ev) {
            case WsInstance::EventType::connect: action = "connect";break;
            case WsInstance::EventType::disconnect: action = "disconnect";break;
            case WsInstance::EventType::data: action = "data";break;
            case WsInstance::EventType::exception: action = "exception";break;
            default: break;
        }
        std::cout << evtype << " " << action << " ";
        if (v.defined()) v.toStream(std::cout);
        std::cout << std::endl;
    });

    client.login({
        ACCOUNT_NUMBER,
        ACCOUNT_PASSWORD,
        "mmbot",
        [](const auto &...){}
    }, true);
/*
    auto sub = client.subscribe_quotes("BITCOIN", [&](const XTBStreaming::Quote &qt){
        std::lock_guard _(console_lock);
        std::cout << "Quote:  bid: " << qt.bid << ", ask " <<  qt.ask << std::endl;
    });*/
    auto poscntr = PositionControl::subscribe(client, [&](PositionControl &pc, const std::string &symbol){
        auto position = pc.getPosition(symbol);
        std::cout << "Position change: symbol=" << symbol << ", position=" << position.getPos() << ", open_price=" << position.getOpen() << std::endl;
        while (pc.any_trade()) {
            auto t = pc.pop_trade();
            std::cout << "Trade: id=" << t.id << ", symbol=" << t.symbol << ", price=" << t.price << ", size=" << t.size << std::endl;
        }
    });

    std::cout << "Trade amount (+long, -short), 'q' = quit" << std::endl;
    std::string s;
    do {
        std::getline(std::cin, s);
        if (s == "q") break;
        if (s.empty()) continue;
        double pos = std::strtod(s.c_str(),nullptr);
        if (pos) {
            poscntr->execute_trade("BITCOIN", pos, 1, XTBExecutor(client));
            std::cout << "Change position delta:" << pos << std::endl;
        } else {
            std::cout << "No position change" << std::endl;
        }
    } while (true);


    return 0;
}
