/*
 * mtrader3.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADER_H_
#define SRC_MAIN_TRADER_H_

#include <optional>
#include <string>
#include <imtjson/value.h>
#include "stats2report.h"

#include "acb.h"

#include "istatsvc.h"

#include "walletDB.h"

#include "istorage.h"
#include "hist_data_storage.h"

#include "spreadgenerator.h"
#include "strategy3.h"

class IStockApi;


enum class SwapMode3 {
	no_swap = 0,
	swap = 1,
	invert = 2
};

struct HistMinuteDataItem {
	std::uint64_t timestamp;
	double price;
};


struct Trader_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;
	std::string paper_trading_src_state;

	double min_size;
	double max_size;
	std::optional<double> min_position;
	std::optional<double> max_position;
	std::optional<double> max_costs;

	double report_order;
	double max_leverage;
	SwapMode3 swap_mode;


	double init_open;

	bool paper_trading;
	bool dont_allocate;
	bool enabled;
	bool hidden;
	bool trade_within_budget;
};

struct Trader_Env {
	Strategy3 strategy;
	SpreadGenerator spread_gen;
	PStockApi exchange;
	PStatSvc statsvc;
	PPerfModule perfscv;
	PStorage state_storage;
	PHistStorage<HistMinuteDataItem> histData;
	PWalletDB walletDB;
	PWalletDB accumDB;
	PBalanceMap balanceCache;
	PBalanceMap externalBalance;
	PBalanceMap conflicts;

};

PStockApi selectStock(PStockApi source,SwapMode3 swap_mode,bool paper_trading);

class Trader {
public:

	using Trade = IStatSvc::TradeRecord;
	using Trades = std::vector<Trade>;

public:


	Trader(const Trader_Config &cfg, Trader_Env &&env);
	void init();
	void update_minfo();

	void run();


public:
	//backtest reporting

	Strategy3 get_stategy() const;
	SpreadGenerator get_spread() const;
	json::Value get_strategy_report() const;
	double get_strategy_position() const;
	const ACB &get_position() const;
	const ACB &get_position_offset() const;
	double get_equity_allocation() const;
	double get_currency_allocation() const;
	double get_equilibrium() const;
	double get_neutral_price() const;
	double get_complete_trades() const;
	double get_last_trade_eq_extra() const;




protected:

	class TradesArray: public AbstractArray<IStockApi::Trade> {
	public:
		TradesArray(Trader &owner):owner(owner) {}
		virtual std::size_t size() const override;
		virtual IStockApi::Trade operator [](std::size_t idx) const override;
		virtual ~TradesArray() {}

	protected:
		Trader &owner;
	};


	struct MarketStateEx: public MarketState {
		double broker_assets;
		double broker_currency;
	};

	struct LimitOrder {
		double price;
		double size;
	};

	class Control;

	struct OrderPair {
		std::optional<IStockApi::Order> buy;
		std::optional<IStockApi::Order> sell;
	};

	struct ScheduledOrder: public LimitOrder {
		json::Value replace_id;
		double replace_size;
	};

	class ScheduledOrders : public std::vector<ScheduledOrder> {
	public:
		void cancel_order(json::Value id) {push_back({0,0,id,0});}
		void cancel_order(const std::optional<IStockApi::Order> &prev_order) {
			if (prev_order.has_value()) push_back({0,0,prev_order->id,0});
		}
		void place_limit(double price, double size) {push_back({price,size,nullptr,0});}
		void edit_limit(json::Value id, double excepted_size, double price, double size){
			push_back({price,size,id,excepted_size});
		}
		void place_limit(double price, double size, const std::optional<IStockApi::Order> &replace) {
			if (replace.has_value()) edit_limit(replace->id, std::abs(replace->size), price,size);
			else place_limit(price,size);
		}
		void place_market(double size) {push_back({0,size,nullptr,0});}
	};


	///trader's configuration
	const Trader_Config cfg;
	///trader's environment - external objects (which can by changed)
	Trader_Env env;
	///Market information
	IStockApi::MarketInfo minfo;
	///ID of last trade - for syncing trades
	json::Value trade_lastid;
	///Magic number used to mark orders
	std::size_t magic;
	///Magic number used to mark secondary orders
	std::size_t magic2;
	///Trader's unique ID. It is generated on creation as random number
	std::size_t uid;
	///this field is true when instance has been inited and prepared to run, false if not yet
	bool inited = false;

	///current trader position
	double position = 0;
	///unconfirmed position - contains same as position except, when detected lost trade, they can be different
	double unconfirmed_position = 0;
	///last known live position - can be complete different, but used to detect lost trades
	double last_known_live_position = 0;
	///last known live balance - can be complete different, but used to detect lost trades
	double last_known_live_balance= 0;

	double equilibrium = 0;

	double eq_allocation = 0;

	double cur_allocation = 0;

	double neutral_price = 0;

	///equity extra on last trade (pnl - eq_allocation)
	/** Must be persistent. The actual value is not important, however it is important
	 * to track changes. Difference between two values (after trade) is reported
	 * as change in normalized profit.
	 */
	std::optional<double> last_trade_eq_extra ;



	///last trade price - reported to strategy
	double last_trade_price = 0;
	///last trade size - reported to strategy
	double last_trade_size = 0;

	///spent currency
	double spent_currency = 0;
	///current unconfirmed position difference - in case of partial execution
	ACB pos_diff;
	bool position_valid = false;
	ACB acb_state;
	///record of trades
	Trades trades;
	///abstract array to access trades by strategy
	TradesArray trarr;
	///count of confirmed trades (from trades)
	std::size_t completted_trades = 0;

	std::uint64_t prevTickerTime = 0;

	///target amounts, fetch from orders to determine, when report trade to the strategy
	std::optional<double> target_buy, target_sell;

	///Buy was previously rejected
	bool rej_buy = false;
	///Sell was previously rejected
	bool rej_sell = false;
	///active on first cycle;
	bool first_run = true;


	MinMax safeRange;

	///report generated by strategy
	json::Value strategy_report_state;


	std::vector<IStockApi::NewOrder> newOrders;
	std::vector<json::Value> newOrders_ids;
	std::vector<std::string> newOrders_err;

	void load_state();
	void updateEnterPrice();
	void save_state();

	bool processTrades();
	MarketStateEx getMarketState();
	void close_all_orders();
	void detect_lost_trades(bool any_trades, const MarketStateEx &mst);

	ScheduledOrders schOrders;

	std::optional<IStockApi::Orders> openOrderCache;
	OrderPair fetchOpenOrders(std::size_t magic);

	///Schedule orders generated by strategy
	/**
	 * @param cntr control object
	 * @retval true scheduled
	 * @retval false can't continue, commit trade event
	 */
	void placeAllOrders(const Control &cntr, const OrderPair &pair);
	bool isSameOrder(const std::optional<IStockApi::Order> &curOrder, const LimitOrder &newOrder) const;
};


#endif /* SRC_MAIN_TRADER_H_ */
