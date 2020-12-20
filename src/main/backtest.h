/*
 * backtest.h
 *
 *  Created on: 18. 1. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_BACKTEST_H_
#define SRC_MAIN_BACKTEST_H_

#include <imtjson/value.h>
#include <functional>
#include <optional>

#include "mtrader.h"

struct BTPrice {
	std::uint64_t time = 0;
	double price = 0;
};

enum class BTEvent {
	no_event,
	margin_call,
	liquidation,
	no_balance,
	accept_loss,
	error

};

struct BTTrade {
	BTPrice price;
	double size = 0;
	double norm_profit = 0;
	double norm_accum = 0;
	double neutral_price = 0;
	double open_price = 0;
	double pl = 0;
	double pos = 0;
	double norm_profit_total = 0;
	BTEvent event = BTEvent::no_event;

	json::Value info;
};

using BTPriceSource = std::function<std::optional<BTPrice>()>;
using BTTrades = std::vector<BTTrade>;

class IStockSelector;


BTTrades backtest_cycle(const MTrader_Config &config, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, std::optional<double> init_pos, double balance, bool negbal);



#endif /* SRC_MAIN_BACKTEST_H_ */
