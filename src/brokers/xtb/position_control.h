#pragma once
#ifndef SRC_BROKERS_XTB_POSITION_CONTROL_H_
#define SRC_BROKERS_XTB_POSITION_CONTROL_H_
#include "client.h"
#include "types.h"
#include "../../main/acb.h"
#include "../../main/sgn.h"

#include <queue>
#include <memory>

enum class CloseOrdering {
    fifo = 0,
    small_first = 1,
    profit_first = 2,
    loss_first = 3,
};

class PositionControl {
public:


    PositionControl();


    struct Trade {
        std::string symbol;
        std::string id;
        double price;
        double size;
        double commision;
        Time time;
    };

    bool on_trades(const std::vector<Position> &pos) ;
    bool any_trade() const;
    Trade pop_trade();

    ACB getPosition(const std::string &symbol) const;
    std::vector<std::pair<std::string, ACB> > getPositionSummary() const;

    template<typename CB>
    static std::shared_ptr<PositionControl> subscribe(XTBClient &client, CB cb);

    struct Cmd {
        Position::Command cmd;
        Position::Type type;
        OrderID order2;
        double volume;
        double price_hint;
    };

    void set_close_ordering(CloseOrdering close_ordering) {
        _close_ordering = close_ordering;
    }
    CloseOrdering get_close_ordering() const {
        return _close_ordering ;
    }

    template<typename CB>
    void execute_trade(const std::string &symbol, double size, double price_hint,  CB &&cb_CMD);

    unsigned int get_trade_counter() const;

    bool check_consistency(const PositionControl &backup, unsigned int &trade_counter) const;

    void update_from_backup(const PositionControl &backup) ;

    void refresh(XTBClient &client);

protected:

    unsigned int _trade_counter;

    using OpenPosition = std::vector<Position>;
    using SymbolPosMap = std::unordered_map<std::string, OpenPosition>;

    SymbolPosMap _symbol_pos_map;
    mutable std::mutex _mx;
    XTBClient::TradeSubscription _sub;
    std::atomic<CloseOrdering> _close_ordering = CloseOrdering::small_first;

    void on_open(const Position &pos);
    void on_close(const Position &pos);

    std::queue<Trade> _trades;
    static double signByCmd(Position::Command cmd);
    static std::string gen_id(const Position &pos);

    std::vector<const Position *> _close_candidate;

    ACB aggregate_position(const OpenPosition &lst) const;


    static auto position_profit(const Position &p, double price) {
        return (price - p.open_price) * p.volume;
    }
};

template<typename CB>
inline std::shared_ptr<PositionControl> PositionControl::subscribe(XTBClient &client, CB cb) {
    auto pc = std::make_shared<PositionControl>();
    pc->_sub = client.subscribe_trades([cb = std::move(cb), wk = std::weak_ptr<PositionControl>(pc)](const std::vector<Position> &pos) mutable {
        auto pc = wk.lock();
        if (pc) {
            if (pc->on_trades(pos)) {
                cb(*pc);
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

    _close_candidate.clear();
    for (const auto &p: iter->second) if (p.cmd == close_cmd) {
        _close_candidate.push_back(&p);
    }
    switch (_close_ordering) {
        default:break;
        case CloseOrdering::small_first:std::stable_sort(_close_candidate.begin(), _close_candidate.end(),
                            [&](const Position *p1, const Position *p2) {
                                return p1->volume < p2->volume;
                            });
                            break;
        case CloseOrdering::loss_first: std::stable_sort(_close_candidate.begin(), _close_candidate.end(),
                [&](const Position *p1, const Position *p2) {
                    return position_profit(*p1, price_hint) < position_profit(*p2, price_hint);
                });
                break;
        case CloseOrdering::profit_first: std::stable_sort(_close_candidate.begin(), _close_candidate.end(),
                [&](const Position *p1, const Position *p2) {
                    return position_profit(*p1, price_hint) > position_profit(*p2, price_hint);
                });
                break;
    }

    const Position *first_skipped = nullptr;
    for (const auto *posptr: _close_candidate) {
        const Position &pos = *posptr;
        if (similar(pos.volume, sz)) {
            cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, pos.volume,price_hint});
            sz = 0.0;
        } else if (pos.volume < sz) {
            cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, pos.volume,price_hint});
            sz -= pos.volume;
        } else if (!first_skipped) {
            first_skipped = posptr;
        }
    };
    if (sz > 0.0) {
        if (first_skipped) {
            const Position &pos = *first_skipped;
            cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, sz,price_hint});
        } else {
            cb_CMD(symbol, Cmd{cmd, Position::Type::OPEN, 0, sz,price_hint});
        }
    }

/*
    const auto &lst = iter->second;
    _skipped.clear();
    for (const Position &pos: lst) {
        if (pos.cmd == close_cmd) {
            if (similar(pos.volume, sz)) {
                cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, pos.volume,price_hint});
                sz = 0.0;
            } else if (pos.volume < sz) {
                cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, pos.volume,price_hint});
                sz -= pos.volume;
            } else {
                _skipped.push_back(&pos);
            }
        }
    }
    if (sz > 0.0) {
        if (!_skipped.empty()) {
            const Position &pos = *_skipped.front();
            cb_CMD(symbol, Cmd{pos.cmd, Position::Type::CLOSE, pos.order, sz,price_hint});
        } else {
            cb_CMD(symbol, Cmd{cmd, Position::Type::OPEN, 0, sz,price_hint});
        }
    }
    */
}

class XTBExecutor {
public:

    XTBExecutor(XTBClient &client):_client(client) {}

    void operator()(const std::string &symbol, const PositionControl::Cmd &c) const;

protected:
    XTBClient &_client;
};


#endif /* SRC_BROKERS_XTB_POSITION_CONTROL_H_ */
