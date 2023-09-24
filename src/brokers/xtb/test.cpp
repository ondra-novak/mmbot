#include "test.h"
#include "client.h"
#include "position_control.h"
#include <simpleServer/http_client.h>

#include "assets.h"

#include "ratio.h"
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
    auto poscntr = PositionControl::subscribe(client, [&](PositionControl &pc){
        std::lock_guard _(console_lock);
        if (!pc.any_trade()) {
            auto sum = pc.getPositionSummary();
            for (const auto &[symbol, position]: sum) {
                std::cout << "Position status: symbol=" << symbol << ", position=" << position.getPos() << ", open_price=" << position.getOpen() << std::endl;
            }
        }
        while (pc.any_trade()) {
            auto t = pc.pop_trade();
            std::cout << "Trade: id=" << t.id << ", symbol=" << t.symbol << ", price=" << t.price << ", size=" << t.size << std::endl;
            auto position = pc.getPosition(t.symbol);
            std::cout << "Position change: symbol=" << t.symbol << ", position=" << position.getPos() << ", open_price=" << position.getOpen() << std::endl;
        }
    });

//    XTBAssets assets;
//    assets.update(client);
//    auto fxconv = assets.get_ratio("BTC", "CZK", client);
//    fxconv->wait();
    RatioTable rtable;
    auto tradeStatus = client.subscribe_tradeStatus([&](const std::vector<TradeStatus> &st){
        std::lock_guard _(console_lock);
        for (const auto &s: st) {
            std::cout << "Trade Status: order=" << s.order <<", status=" << static_cast<int>(s.status)
                    <<", price=" << s.price
                    <<", message=" << s.message << std::endl;
        }
    });


    std::cout << "Trade amount (+long, -short), 'q' = quit, 'r' = refresh position" << std::endl;
    std::string s;
    do {
/*        {
            double rate = fxconv->get_rate();
            std::lock_guard _(console_lock);
            std::cout << "Current rate is: " << rate << std::endl;
        }*/
        std::getline(std::cin, s);
        if (s == "q") break;
        if (s == "r") {
            poscntr->refresh(client);
            continue;
        }
  /*      if (s.find("->") != s.npos) {
            auto np = s.find("->");
            std::string from = s.substr(0,np);
            std::string to = s.substr(np+2);
            double rr = rtable.get_ratio({from, to}, assets, client);
            std::lock_guard _(console_lock);
            std::cout << "Ratio " << from << " -> " << to << " = " << rr << std::endl;
        }*/
        if (s.empty()) continue;
        double pos = std::strtod(s.c_str(),nullptr);
        if (pos) {
            poscntr->execute_trade("BITCOIN", pos, 1, XTBExecutor(client));
            std::lock_guard _(console_lock);
            std::cout << "Change position delta:" << pos << std::endl;
        } else {
            std::lock_guard _(console_lock);
            std::cout << "No position change" << std::endl;
        }
    } while (true);


    return 0;
}
