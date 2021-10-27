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

#include <shared/ini_config.h>
#include <imtjson/namedEnum.h>
#include "dynmult.h"
#include "idailyperfmod.h"
#include "istatsvc.h"
#include "storage.h"
#include "report.h"
#include "spread.h"
#include "strategy.h"
#include "walletDB.h"

class IStockApi;



struct MTrader_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;

	double buy_step_mult;
	double sell_step_mult;
	double min_size;
	double max_size;
	std::optional<double> min_balance;
	std::optional<double> max_balance;

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

	bool dry_run;
	bool internal_balance;
	bool dont_allocate;
	bool enabled;
	bool hidden;
	bool dynmult_sliding;
	bool dynmult_mult;
	bool swap_symbols;
	bool reduce_on_leverage;

	Strategy strategy = Strategy(nullptr);

	void loadConfig(json::Value data, bool force_dry_run);

};

struct WalletCfg {
	PWalletDB walletDB;
	PWalletDB accumDB;
	PBalanceMap balanceCache;
	PBalanceMap externalBalance;
};

class MTrader {
public:

	using StoragePtr = PStorage;
	using Config = MTrader_Config;

	struct Order: public Strategy::OrderData {
		bool isSimilarTo(const IStockApi::Order &other, double step, bool inverted);
		Order(const Strategy::OrderData& o):Strategy::OrderData(o) {}
		Order() {}
		Order(double size, double price, IStrategy::Alert alert)
			:Strategy::OrderData {price, size, alert} {}
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
		std::optional<IStockApi::Order> buy,sell,buy2,sell2;
	};

	struct ZigZagInfo {
		//total amount covered by this level
		double amount;
		//average price on this level
		double price;
	};

	struct ZigZagLevels {
		//zigzag direction
		double direction;
		//levels
		std::vector<ZigZagInfo> levels;
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
	void setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<double> &alert, bool secondary);


	using ChartItem = IStatSvc::ChartItem;
	using Chart = std::vector<ChartItem>;


	struct Status {
		IStockApi::Ticker ticker;
		double curPrice;
		double curStep;
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

		double new_fees;
		double spreadCenter;
		IStockApi::TradesSync new_trades;
		ChartItem chartItem;
		std::size_t enable_alerts_after_minutes;
		///asset balance reported by the broker - not defined for internal balance = on
		std::optional<double> brokerAssetBalance;
		///currency balance reported by the broker - not defined for internal balance = on
		std::optional<double> brokerCurrencyBalance;
	};

	Status getMarketStatus() const;

	Order calculateOrder(
			Strategy state,
			double lastTradePrice,
			double step,
			double dynmult,
			double curPrice,
			double balance,
			double currency,
			bool alerts) const;
	Order calculateOrderFeeLess(
			Strategy state,
			double lastTradePrice,
			double step,
			double dynmult,
			double curPrice,
			double balance,
			double currency,
			bool alerts) const;
	bool calculateOrderFeeLessAdjust(Order &order,double assets, double currency,
			int dir, bool alert, double min_size) const;


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



	std::optional<double> getInternalBalance() const;
	std::optional<double> getInternalCurrencyBalance() const;
	std::optional<double> getPosition() const;
	std::optional<double> getCurrency() const;


	void saveState();
	void addAcceptLossAlert();

	auto getUID() const {return uid;}
	bool isInitialResetRequired() const {return need_initial_reset;}
	double getAccumulated() const;

	void recalcNorm();
	void fixNorm();

protected:

	PStockApi stock;
	Config cfg;
	IStockApi::MarketInfo minfo;
	StoragePtr storage;
	PStatSvc statsvc;
	WalletCfg wcfg;
	Strategy strategy;
	DynMultControl dynmult;
	bool need_load = true;
	bool recalc = true;
	bool first_cycle = true;
	bool achieve_mode = false;
	bool need_initial_reset = true;
	unsigned int adj_wait = 0;
	double adj_wait_price = 0;
	double lastPriceOffset = 0;
	double lastTradePrice = 0;
	json::Value test_backup;
	json::Value lastTradeId = nullptr;
	std::optional<double> sell_alert, buy_alert;

	using TradeItem = IStockApi::Trade;
	using TWBItem = IStatSvc::TradeRecord;

	std::vector<ChartItem> chart;
	TradeHistory trades;

	double position = 0;
	double currency = 0;
	double accumulated = 0;
	bool position_valid = false;
	bool currency_valid = false;


/*	std::optional<double> asset_balance;
	std::optional<double> currency_balance;*/

	size_t magic = 0;
	size_t magic2 = 0;
	size_t uid = 0;
	PerformanceReport tempPr;

	void loadState();

	double raise_fall(double v, bool raise) const;

	static PStockApi selectStock(IStockSelector &stock_selector, const Config &conf);

	bool processTrades(Status &st);

	void update_dynmult(bool buy_trade,bool sell_trade);
	static void alertTrigger(Status &st, double price);

	void acceptLoss(const Status &st, double dir);
	json::Value getTradeLastId() const;


	struct SpreadCalcResult {
		double spread;
		double center;
	};


	SpreadCalcResult calcSpread() const;
	bool checkMinMaxBalance(double newBalance, double dir) const;
	std::pair<bool, double> limitOrderMinMaxBalance(double balance, double orderSize) const;

	ZigZagLevels zigzaglevels;


	bool checkLeverage(const Order &order, double assets, double currency, double &maxSize) const;

	WalletDB::Key getWalletBalanceKey() const;
	WalletDB::Key getWalletAssetKey() const;


private:
	template<typename Iter>
	static SpreadCalcResult stCalcSpread(Iter beg, Iter end, unsigned int input_sma, unsigned int input_stdev);


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
};




#endif /* SRC_MAIN_MTRADER_H_ */
