#include "position_control.h"

#include <imtjson/object.h>
#include <algorithm>
PositionControl::PositionControl()

{

}

bool PositionControl::on_trade(const Position &pos) {
    std::lock_guard _(_mx);
    switch(pos.type) {
        case Position::Type::OPEN:
            on_open(pos);
            return true;
        case Position::Type::CLOSE:
            on_close(pos);
            return true;
        default:
            return false;
    }

}

void PositionControl::on_open(const Position &pos) {
    auto &p = _symbol_pos_map[pos.symbol];
    auto iter = std::find_if(p.begin(), p.end(), [&](const Position &z){
        return z.order2 == pos.position;
    });
    if (iter == p.end()) {
        p.push_back(pos);
        if (!pos.snapshot) {
            _trades.push({
                pos.symbol,
                gen_id(pos),
                pos.open_price,
                pos.volume*signByCmd(pos.cmd)
            });
        }
    } else {
        //position can't change unless there is partial close
        //in this case, closed position will be processed
    }
}

ACB PositionControl::getPosition(const std::string &symbol) const {
    std::lock_guard _(_mx);
    ACB pos{0,0,0};
    auto iter = _symbol_pos_map.find(symbol);
    if (iter != _symbol_pos_map.end())  {
        const OpenPosition &p = iter->second;
        for (const auto &x: p) {
            double sign = signByCmd(x.cmd);
            double sz = x.volume;
            double price = x.open_price;
            pos = pos(price, sz * sign);
        }
    }
    return pos;
}

bool PositionControl::any_trade() const {
    return !_trades.empty();
}

PositionControl::Trade PositionControl::pop_trade() {
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
            -pos.volume*signByCmd(pos.cmd)
        });
        auto &p = _symbol_pos_map[pos.symbol];
        auto iter = std::find_if(p.begin(), p.end(), [&](const Position &z){
             return z.position == pos.position;
         });
        if (iter != p.end()) {
            auto remain = iter->volume - pos.volume;
            if (remain <= 1e-20) {
                p.erase(iter);
            }
        }
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
    }
}
