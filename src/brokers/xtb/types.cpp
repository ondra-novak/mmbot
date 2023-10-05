#include "types.h"

#include <imtjson/value.h>

Position Position::fromJSON(json::Value v, bool snapshot) {

    Position pos;
    pos.close_price = v["close_price"].getNumber();
    pos.close_time = v["close_time"].getUIntLong();
    pos.closed = v["closed"].getBool();
    pos.cmd = static_cast<Command>(v["cmd"].getUInt());
    pos.comment = v["comment"].getString();
    pos.custom_comment = v["customComment"].getString();
    pos.commission = v["commission"].getNumber();
    pos.digits = v["digits"].getUInt();
    pos.expiration = v["expiration"].getUIntLong();
    pos.margin_rate = v["margin_rate"].getNumber();
    pos.open_price = v["open_price"].getNumber();
    pos.open_time = v["open_time"].getUIntLong();
    pos.order = v["order"].getUIntLong();
    pos.order2 = v["order2"].getUIntLong();
    pos.position= v["position"].getUIntLong();
    pos.profit = v["profit"].getNumber();
    pos.sl= v["sl"].getNumber();
    pos.state= v["state"].getString() == "Modified"?State::Modified:State::Deleted;
    pos.storage = v["storage"].getNumber();
    pos.symbol= v["symbol"].getString();
    pos.tp = v["tp"].getNumber();
    pos.type = static_cast<Type>(v["type"].getUInt());
    pos.volume = v["volume"].getNumber();
    pos.snapshot = snapshot;
    return pos;
}
