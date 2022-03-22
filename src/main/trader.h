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
	std::optional<double> min_balance;
	std::optional<double> max_balance;
	std::optional<double> max_costs;

	double report_order;
	double max_leverage;


	double init_open;

	bool paper_trading;
	bool dont_allocate;
	bool enabled;
	bool hidden;
	bool trade_within_budget;
};

struct Trader_Env {
	Strategy3 strategy;
	PStatSvc statsvc;
	SpreadGenerator spread_gen;
	PStockApi exchange;
	PStorage state_storage;
	PHistStorage<HistMinuteDataItem> histData;
	PWalletDB walletDB;
	PWalletDB accumDB;
	PBalanceMap balanceCache;
	PBalanceMap externalBalance;
	PBalanceMap conflicts;

};


class Trader {
public:

	using Trade = IStatSvc::TradeRecord;
	using Trades = std::vector<Trade>;

public:


	Trader(const Trader_Config &cfg, Trader_Env &&env);
	void init();
	void update_minfo();

	void run();


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
	double unconfirmed_position_diff = 0;
	///last known live position - can be complete different, but used to detect lost trades
	double last_known_live_position = 0;
	///last known live balance - can be complete different, but used to detect lost trades
	double last_known_live_balance= 0;

	double equilibrium = 0;

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

	///Buy was previously rejected
	bool rej_buy = false;
	///Sell was previously rejected
	bool rej_sell = false;

	void load_state();
	void updateEnterPrice();
	void save_state();

	void processTrades();
	MarketStateEx getMarketState();

};


#endif /* SRC_MAIN_TRADER_H_ */
