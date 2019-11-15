/*
 * market.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */




#include "market.h"

Market::Market(const PQuoteDistributor &qdist, CmdFn  &&cmdfn):qdist(qdist),cmdfn(std::move(cmdfn)) {
}

PTradingEngine Market::getMarket(const std::string_view &symbol) {
	std::string s(symbol);
	auto &m = markets[s];
	if (m == nullptr) {
		m = TradingEngine::create([this,s](double v) {
			return this->cmdfn(s, v);
		});
		m->start(qdist->createRegFn(symbol));
	}
	return m;
}
