/*
 * mtrader.h
 *
 *  Created on: 15. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_MTRADER_H_
#define SRC_MAIN_MTRADER_H_
#include <deque>
#include <optional>
#include <type_traits>
#include <limits>

#include <shared/ini_config.h>
#include <imtjson/namedEnum.h>
#include "acb.h"

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


enum class SwapMode {
	no_swap = 0,
	swap = 1,
	invert = 2
};

struct MTrader_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;

	std::string paper_trading_src_state;

	double min_size;
	double max_size;
	std::optional<double> min_balance;
	std::optional<double> max_balance;
	std::optional<double> max_costs;


	unsigned int adj_timeout;

	double report_order;
	double max_leverage;
	double emulate_leveraged;
	double secondary_order_distance;
	unsigned int grant_trade_minutes;


	double init_open;

	SwapMode swap_mode;

	bool paper_trading;
	bool dont_allocate;
	bool enabled;
	bool hidden;
	bool trade_within_budget;

	Strategy strategy = Strategy(nullptr);
	clone_ptr<ISpreadGen> spread;

	void loadConfig(json::Value data);

};

struct WalletCfg {
	PWalletDB walletDB;
	PWalletDB accumDB;
	PBalanceMap balanceCache;
	PBalanceMap externalBalance;
	PBalanceMap conflicts;
};

class MTrader {
public:

	using StoragePtr = PStorage;
	using Config = MTrader_Config;

	struct Order: public Strategy::OrderData {
		AlertReason ar = AlertReason::unknown;
		bool isSimilarTo(const IStockApi::Order &other, double step, bool inverted);
		Order(const Strategy::OrderData& o, AlertReason ar):Strategy::OrderData(o),ar(ar) {}
		Order() {}
		Order(double size, double price, IStrategy::Alert alert, AlertReason ar)
			:Strategy::OrderData {price, size, alert},ar(ar) {}
		void update(IStockApi::Order &o) {
			o.price = price;
			o.size = size;
			o.id = json::undefined;
			o.client_id = json::undefined;
		}
		void update(std::optional<IStockApi::Order> &o) {
			o.emplace(IStockApi::Order{
				json::undefined,
				json::undefined,
				size,
				price,
			});
		}
	};

	struct OrderPair {
		std::optional<IStockApi::Order> buy,sell;
	};




	MTrader(IStockSelector &stock_selector,
			StoragePtr &&storage,
			PStatSvc &&statsvc,
			const WalletCfg &walletCfg,
			Config config);



	void perform(bool manually);

	void init();
	bool need_init() const;

	OrderPair getOrders();
	void setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<AlertInfo> &alert, bool secondary);


	using ChartItem = IStatSvc::ChartItem;
	using Chart = std::vector<ChartItem>;


	struct Status {
		IStockApi::Ticker ticker;
		double curPrice;
		///asset balance allocated for this trader (including external assets)
		double assetBalance;
		///total asset balance available for all traders (including external assets)
		double assetUnadjustedBalance;
		///available balance on exchange (external assets not counted) for this trader
		double assetAvailBalance;
		///current balance allocated for this trader (including external assets)
		double currencyBalance;
		///current balance available for all traders (including external assets)
		double currencyUnadjustedBalance;
		///available balance on exchange (external assets not counted) for this trader
		double currencyAvailBalance;

		IStockApi::TradesSync new_trades;
		ChartItem chartItem;
		std::size_t enable_alerts_after_minutes;
		///asset balance reported by the broker - not defined for internal balance = on
		std::optional<double> brokerAssetBalance;
		///currency balance reported by the broker - not defined for internal balance = on
		std::optional<double> brokerCurrencyBalance;
	};

	Status getMarketStatus() const;

    bool calculateOrderFeeLessAdjust(Order &order,double assets, double currency,
            int dir, bool alert, double asset_fees, bool no_leverage_check = false) const;


	Config getConfig() {return cfg;}

	const IStockApi::MarketInfo &getMarketInfo() const {return minfo;}

	bool eraseTrade(std::string_view id, bool trunc);
	void clearStats();
//	void reset(std::optional<double> achieve_pos = std::optional<double>());

	struct ResetOptions {
		//currency percentage (0-100)
		double cur_pct;
		//assets to achieve - only when achieve is true
		double assets;
		//achieve given position
		bool achieve;
	};
	void reset(const ResetOptions &opt);


	Chart getChart() const;
	void dropState();
	void stop();

	using TradeHistory = std::vector<IStatSvc::TradeRecord>;

	const TradeHistory &getTrades() const;


	Strategy getStrategy() const {return strategy;}
	void setStrategy(const Strategy &s) {strategy = s;}
	void setInternalBalancies(double assets, double currency);

	PStockApi getBroker() const {return stock;}

	struct VisRes {
		struct Item {
			double price, low, high, size;
			std::uint64_t time;
		};
		std::vector<Item> chart;
	};



	std::optional<double> getPosition() const;
	std::optional<double> getCurrency() const;
	double getEnterPrice() const;
	double getEnterPricePos() const;
	double getCosts() const;
	double getRPnL() const;
	double getPartialPosition() const;


	void saveState();
	void addAcceptLossAlert();

	auto getUID() const {return uid;}
	bool isInitialResetRequired() const {return need_initial_reset;}
	double getAccumulated() const;

	void recalcNorm();
	void fixNorm();

	static PStockApi selectStock(IStockSelector &stock_selector, std::string_view broker_name, SwapMode swap_mode, int emulate_leverage, bool paper_trading);
	json::Value getOHLC(std::uint64_t interval) const;

	void set_trade_now(bool t) {
	    trade_now_mode = t;
	}

protected:

	PStockApi stock;
	Config cfg;
	IStockApi::MarketInfo minfo;
	StoragePtr storage;
	PStatSvc statsvc;
	WalletCfg wcfg;
	Strategy strategy;
	bool need_load = true;
	bool recalc = true;
	bool first_cycle = true;
	bool achieve_mode = false;
	bool trade_now_mode = false;
	bool need_initial_reset = true;
	unsigned int adj_wait = 0;
	double adj_wait_price = 0;
	double lastPriceOffset = 0;
	double lastTradePrice = 0;
	json::Value lastTradeId = nullptr;
	std::optional<AlertInfo> sell_alert, buy_alert;

	using TradeItem = IStockApi::Trade;
	using TWBItem = IStatSvc::TradeRecord;

	std::vector<ChartItem> chart;
	TradeHistory trades;
	clone_ptr<ISpreadGen::State> spread_state;

	double position = 0;
	double currency = 0;
	double accumulated = 0;
	double spent_currency = 0;
	double strategy_position = 0; //position reported to strategy - must be updated after process trades
	ACB acb_state;
	ACB partial_eff_pos;
	double partial_position = 0;
	double target_buy_size = 0;
	double target_sell_size = 0;
	bool position_valid = false;
	bool currency_valid = false;
	bool refresh_minfo = false;


/*	std::optional<double> asset_balance;
	std::optional<double> currency_balance;*/

	size_t magic = 0;
	size_t magic2 = 0;
	size_t uid = 0;
	PerformanceReport tempPr;

	void loadState();


	bool processTrades(Status &st);

	void alertTrigger(const Status &st, double price, int dir, AlertReason reason);

	json::Value getTradeLastId() const;



	bool checkMinMaxBalance(double newBalance, double dir, double price) const;
	std::pair<AlertReason, double> limitOrderMinMaxBalance(double balance, double orderSize, double price) const;



	AlertReason checkLeverage(const Order &order, double assets, double currency, double &maxSize) const;

	WalletDB::Key getWalletBalanceKey() const;
	WalletDB::Key getWalletAssetKey() const;


private:

	void initialize();
	mutable std::uint64_t period_cache = 0;

	bool checkAchieveModeDone(const Status &st);
	bool checkEquilibriumClose(const Status &st, double lastTradePrice);
	void dorovnani(Status &st, double assetBalance, double price);
	bool checkReduceOnLeverage(const Status &st, double &maxPosition);
	std::unique_ptr<ISpreadFunction> spread_fn;

	enum class BalanceChangeEvent {
		no_change,
		withdraw,
		leak_trade,
		disabled

	};

	BalanceChangeEvent detectLeakedTrade(const Status &st) const;
	void doWithdraw(const Status &st);
	void updateEnterPrice();
	void update_minfo();
    void flush_partial(const Status &status);
    void initializeSpread();
    Order calcBuyOrderSize(const Status &status, double base, double center, bool enable_alerts) const;
    Order calcSellOrderSize(const Status &status, double base, double center, bool enable_alerts) const;
    Order calcOrderTrailer(Order order, double origPrice) const;
};




#endif /* SRC_MAIN_MTRADER_H_ */
