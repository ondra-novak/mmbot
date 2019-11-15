/*
 * market.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_MARKET_H_
#define SRC_SIMPLEFX_MARKET_H_
#include <unordered_map>

#include "quotedist.h"
#include "tradingengine.h"
class Market {
public:
	using CmdFn = std::function<double(const std::string &symbol, double amount)>;

	Market(const PQuoteDistributor &qdist, CmdFn  &&cmdfn);

	PTradingEngine getMarket(const std::string_view &symbol);


protected:
	PQuoteDistributor qdist;
	std::unordered_map<std::string, PTradingEngine> markets;
	CmdFn cmdfn;

};



#endif /* SRC_SIMPLEFX_MARKET_H_ */
