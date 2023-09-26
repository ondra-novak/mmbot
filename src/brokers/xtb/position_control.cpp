#include "position_control.h"

#include <imtjson/object.h>
#include <algorithm>
PositionControl::PositionControl()

{

}

bool PositionControl::on_trades(const std::vector<Position> &trades) {
    if (trades.empty()) return false;
    std::lock_guard _(_mx);
    if (trades.front().snapshot) {
        _symbol_pos_map.clear();
    }
    bool traded = false;
    for (const auto &pos: trades) {
        switch(pos.type) {
            case Position::Type::OPEN:
                on_open(pos);
                traded = true;
                break;
            case Position::Type::CLOSE:
                on_close(pos);
                traded = true;
                break;
            default:
                return false;
        }
    }
    return traded;

}

void PositionControl::on_open(const Position &pos) {
    auto &p = _symbol_pos_map[pos.symbol];
    auto iter = std::find_if(p.begin(), p.end(), [&](const Position &z){
        return z.order2 == pos.order2;
    });
    if (iter == p.end()) {
        p.push_back(pos);
        if (!pos.snapshot && pos.state == Position::State::Modified) {
            _trades.push({
                pos.symbol,
                gen_id(pos),
                pos.open_price,
                pos.volume*signByCmd(pos.cmd),
                0,
                std::chrono::system_clock::now()
            });
        }
    } else {
        if (pos.state == Position::State::Deleted) {
            p.erase(iter);
        } else {
            *iter = pos;
        }
    }
}

ACB PositionControl::getPosition(const std::string &symbol) const {
    std::lock_guard _(_mx);
    auto iter = _symbol_pos_map.find(symbol);
    if (iter != _symbol_pos_map.end())  {
        return aggregate_position(iter->second);
    } else {
        return ACB{0,0,0};
    }
}

ACB PositionControl::aggregate_position(const OpenPosition &lst) const {
    ACB pos{0,0,0};
    for (const auto &x: lst) {
        double sign = signByCmd(x.cmd);
        double sz = x.volume;
        double price = x.open_price;
        pos = pos(price, sz * sign);
    }
    return pos;
}
bool PositionControl::any_trade() const {
    std::lock_guard _(_mx);
    return !_trades.empty();
}

PositionControl::Trade PositionControl::pop_trade() {
    std::lock_guard _(_mx);
   Trade p = std::move(_trades.front());
   _trades.pop();
   return p;
}


void PositionControl::on_close(const Position &pos) {
    if (pos.closed) {
        _trades.push({
            pos.symbol,
            gen_id(pos),
            pos.close_price,
            -pos.volume*signByCmd(pos.cmd),
            pos.commission,
            std::chrono::system_clock::now(),
        });
    }

}

double PositionControl::signByCmd(Position::Command cmd) {
    double sign;
    switch (cmd) {
        case Position::Command::BUY:
        case Position::Command::BUY_STOP:
        case Position::Command::BUY_LIMIT: sign = 1; break;
        case Position::Command::SELL:
        case Position::Command::SELL_LIMIT:
        case Position::Command::SELL_STOP: sign = -1; break;
        default: sign = 0; break;
    }
    return sign;

}

void PositionControl::refresh(XTBClient &client) {
    client.refresh(_sub);
}

std::string PositionControl::gen_id(const Position &pos) {
    return std::to_string(pos.order)+"-"+std::to_string(pos.order2)+"-"+std::to_string(pos.position);
}

void XTBExecutor::operator ()(const std::string &symbol,const PositionControl::Cmd &c) const {
    XTBClient::Result res;
    res = _client("tradeTransaction", json::Object{
        {"tradeTransInfo",json::Object{
           {"cmd",static_cast<int>(c.cmd)},
           {"order",c.order2},
           {"price", c.price_hint},
           {"symbol", symbol},
           {"type", static_cast<int>(c.type)},
           {"volume", c.volume}
        }}
    });
    if (XTBClient::is_result(res)) {
        auto v = XTBClient::get_result(res);
        OrderID order = v["order"].getUIntLong();
        res = _client("tradeTransactionStatus",json::Object{{"order",order}});
        if (_client.is_error(res)) throw _client.get_error(res);
    } else {
        throw _client.get_error(res);
    }
}

std::vector<std::pair<std::string, ACB> > PositionControl::getPositionSummary() const {
    std::lock_guard _(_mx);
    std::vector<std::pair<std::string, ACB> > out;
    for (const auto &[symbol, poslist]: _symbol_pos_map) {
        out.push_back({symbol, aggregate_position(poslist)});
    }
    return out;
}
