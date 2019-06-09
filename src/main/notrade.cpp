/*
 * notrade.cpp
 *
 *  Created on: 9. 6. 2019
 *      Author: ondra
 */

#include "notrade.h"

#include <ctime>
double NoTrade::getBalance(const std::string_view& symb) {
	return 0;
}

NoTrade::TradeHistory NoTrade::getTrades(json::Value lastId, std::uintptr_t fromTime,
		const std::string_view& pair) {
	return {};
}

NoTrade::Orders NoTrade::getOpenOrders(const std::string_view& par) {
	return {};
}

NoTrade::Ticker NoTrade::getTicker(const std::string_view& pair) {
	auto iter = priceMap.find(std::string(pair));
	if (iter == priceMap.end()) {
		priceMap.insert(std::make_pair(std::string(pair),1.0));
		return Ticker({1,1.0001,1,std::size_t(time(nullptr))});
	} else {
		return Ticker({iter->second*0.99999, iter->second * 1.00001, iter->second, std::size_t(time(nullptr)) });
	}
}

json::Value NoTrade::placeOrder(const std::string_view& pair, const Order& order) {
	auto iter = priceMap.find(std::string(pair));
	if (iter != priceMap.end()) {
		iter->second = (iter->second + order.price)/2;
	}
	throw std::runtime_error("NoTrade broker cannot trade");
}

bool NoTrade::reset() {
	return true;
}

bool NoTrade::isTest() const {
	return true;
}

NoTrade::MarketInfo NoTrade::getMarketInfo(const std::string_view& pair) {
	return {"","",1e-8,1e-8,0,0,0};
}

double NoTrade::getFees(const std::string_view& pair) {
	return 0;
}

std::vector<std::string> NoTrade::getAllPairs() {
	return {};
}
