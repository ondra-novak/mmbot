#pragma once
#ifndef SRC_BROKERS_XTB_POSITION_CONTROL_H_
#define SRC_BROKERS_XTB_POSITION_CONTROL_H_
#include "client.h"
#include "types.h"
#include "../../main/acb.h"

#include <queue>
#include <memory>
class PositionControl {
public:


    PositionControl();


    struct Trade {
        std::string symbol;
        std::string id;
        double price;
        double size;
    };

    bool on_trade(const Position &pos);
    bool any_trade() const;
    Trade pop_trade();

    ACB getPosition(const std::string &symbol) const;

    template<typename CB>
    static std::shared_ptr<PositionControl> subscribe(XTBClient &client, CB cb);

    struct Cmd {
        Position::Command cmd;
        Position::Type type;
        OrderID order2;
        double volume;
        double price_hint;
    };

    template<typename CB>
    void execute_trade(const std::string &symbol, double size, double price_hint, CB &&cb_CMD);

protected:


    using OpenPosition = std::vector<Position>;
    using SymbolPosMap = std::unordered_map<std::string, OpenPosition>;

    SymbolPosMap _symbol_pos_map;
    mutable std::mutex _mx;
    XTBClient::TradeSubscription _sub;

    void on_open(const Position &pos);
    void on_close(const Position &pos);

    std::queue<Trade> _trades;
    static double signByCmd(Position::Command cmd);
    static std::string gen_id(const Position &pos);

    std::vector<const Position *> _skipped;

};

template<typename CB>
inline std::shared_ptr<PositionControl> PositionControl::subscribe(XTBClient &client, CB cb) {
    auto pc = std::make_shared<PositionControl>();
    pc->_sub = client.subscribe_trades([cb = std::move(cb), wk = std::weak_ptr<PositionControl>(pc)](const Position &pos) mutable {
        auto pc = wk.lock();
        if (pc) {
            if (pc->on_trade(pos)) {
                cb(*pc, pos.symbol);
            }
        }
    });
    return pc;
}

template<typename CB>
inline void PositionControl::execute_trade(const std::string &symbol, double size, double price_hint, CB &&cb_CMD) {
    std::lock_guard _(_mx);
    if (!size) return ;
    Position::Command cmd = size<0?Position::Command::SELL:Position::Command::BUY;
    Position::Command close_cmd = size>0?Position::Command::SELL:Position::Command::BUY;
    double sz = std::abs(size);
    auto iter = _symbol_pos_map.find(symbol);
    if (iter == _symbol_pos_map.end()) {
        cb_CMD(symbol, Cmd{cmd, Position::Type::OPEN, 0, sz,price_hint});
        return;
    }
    const auto &lst = iter->second;
    _skipped.clear();
    for (const Position &pos: lst) {
        if (pos.cmd == close_cmd) {
            if (pos.volume <= sz + 1e-16) {
                cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, pos.volume,price_hint});
                sz -= pos.volume;
            } else {
                _skipped.push_back(&pos);
            }
        }
    }
    if (sz > 1e-16) {
        if (!_skipped.empty()) {
            const Position &pos = *_skipped.front();
            cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, sz,price_hint});
        } else {
            cb_CMD(symbol, Cmd{cmd, Position::Type::OPEN, 0, sz,price_hint});
        }
    }
}

class XTBExecutor {
public:

    XTBExecutor(XTBClient &client):_client(client) {}

    void operator()(const std::string &symbol, const PositionControl::Cmd &c) const;

protected:
    XTBClient &_client;
};


#endif /* SRC_BROKERS_XTB_POSITION_CONTROL_H_ */
