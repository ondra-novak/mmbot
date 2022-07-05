/*
 * api_v2.cpp
 *
 *  Created on: 3. 7. 2022
 *      Author: ondra
 */


#define MMBOT_BROKER_API_V2

#include "api.h"

[[noreturn]]
static void unsupported() {
    throw std::runtime_error("Unsupported operation in v2 broker");
}

double AbstractBrokerAPI::getBalance(const std::string_view & symb, const std::string_view & pair) {
    unsupported();
}
AbstractBrokerAPI::TradesSync AbstractBrokerAPI::syncTrades(json::Value lastId, const std::string_view & pair) {
    unsupported();
}
AbstractBrokerAPI::Orders AbstractBrokerAPI::getOpenOrders(const std::string_view & par) {
    unsupported();
}
AbstractBrokerAPI::Ticker AbstractBrokerAPI::getTicker(const std::string_view & piar) {
    unsupported();
}
json::Value AbstractBrokerAPI::placeOrder(const std::string_view & pair,
        double size,
        double price,
        json::Value clientId ,
        json::Value replaceId,
        double replaceSize) {
    unsupported();
}
