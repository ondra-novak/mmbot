#pragma once
#ifndef SRC_BROKERS_XTB_TYPES_H_
#define SRC_BROKERS_XTB_TYPES_H_
#include <chrono>
#include <string>

namespace json {
    class Value;
}



class Time :public std::chrono::system_clock::time_point {
public:
    Time() = default;
    Time(const std::chrono::system_clock::time_point &t):std::chrono::system_clock::time_point(t) {}
    Time(std::uint64_t time_ms)
        :std::chrono::system_clock::time_point(
                std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(time_ms)))
    {

    }

    std::uint64_t get_millis() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch()).count();
    }
};

using OrderID = std::uint64_t;
using PositionID = std::uint64_t;


struct Position {
    enum class Command: unsigned char {
        BUY = 0,
        SELL = 1,
        BUY_LIMIT = 2,
        SELL_LIMIT=3,
        BUY_STOP = 4,
        SELL_STOP = 5,
        BALANCE = 6,
        CREDIT = 7
    };

    enum class State: unsigned char {
        Modified,
        Deleted,
    };

    enum class Type: unsigned char {
        OPEN = 0,
        PENDING = 1,
        CLOSE = 2,
        MODIFY = 3,
        DELETE = 4,
    };

    OrderID order;
    OrderID order2;
    PositionID position;
    std::string symbol;
    double open_price;
    double close_price;
    double volume;
    Time open_time;
    Time close_time;
    Command cmd;
    State state;
    Type type;
    bool closed;
    bool snapshot;
    Time expiration;
    double profit;
    double commission;

    unsigned int digits;
    double margin_rate;
    double sl;
    double storage;
    double tp;

    std::string comment;
    std::string custom_comment;

    static Position fromJSON(json::Value v, bool snapshot);
};

struct Quote {
    double bid;
    double ask;
    Time timestamp;
    bool snapshot;
};

struct TradeStatus {

    enum Status {
        ERROR = 0,
        PENDING = 1,
        ACCEPTED = 3,
        REJECTED = 4
    };
    Status status;
    double price;
    std::string message;
    std::string customComment;
    OrderID order;

};





#endif /* SRC_BROKERS_XTB_TYPES_H_ */
