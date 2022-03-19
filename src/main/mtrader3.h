/*
 * mtrader3.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_MTRADER3_H_
#define SRC_MAIN_MTRADER3_H_

#include "dynmult.h"
#include "idailyperfmod.h"
#include "istatsvc.h"
#include "storage.h"
#include "report.h"
#include "spread.h"
#include "strategy.h"
#include "walletDB.h"
#include "alert.h"


class IStockApi;


enum class SwapMode3 {
	no_swap = 0,
	swap = 1,
	invert = 2
};

struct MTrader3_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;

	std::string paper_trading_src_state;

	double buy_step_mult;
	double sell_step_mult;
	double min_size;
	double max_size;
	std::optional<double> min_balance;
	std::optional<double> max_balance;
	std::optional<double> max_costs;

	double dynmult_raise;
	double dynmult_fall;
	double dynmult_cap;
	Dynmult_mode dynmult_mode;

	unsigned int accept_loss;
	unsigned int adj_timeout;

	double force_spread;
	double report_order;
	double max_leverage;
	double emulate_leveraged;
	double secondary_order_distance;
	unsigned int grant_trade_minutes;

	double spread_calc_stdev_hours;
	double spread_calc_sma_hours;

	double init_open;

	SwapMode3 swap_mode;

	bool paper_trading;
	bool dont_allocate;
	bool enabled;
	bool hidden;
	bool dynmult_sliding;
	bool dynmult_mult;
	bool reduce_on_leverage;
	bool freeze_spread;
	bool trade_within_budget;

	Strategy strategy = Strategy(nullptr);

	void loadConfig(json::Value data);

};





#endif /* SRC_MAIN_MTRADER3_H_ */
