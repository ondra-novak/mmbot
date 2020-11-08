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
#include "idailyperfmod.h"
#include "istatsvc.h"
#include "storage.h"
#include "report.h"
#include "strategy.h"
#include "walletDB.h"

class IStockApi;

enum class Dynmult_mode {
	disabled,
	independent,
	together,
	alternate,
	half_alternate,
};


extern json::NamedEnum<Dynmult_mode> strDynmult_mode;

struct MTrader_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;

	double buy_mult;
	double sell_mult;
	double buy_step_mult;
	double sell_step_mult;
	double min_size;
	double max_size;
	std::optional<double> min_balance;
	std::optional<double> max_balance;

	double dynmult_raise;
	double dynmult_fall;
	Dynmult_mode dynmult_mode;

	unsigned int accept_loss;

	double force_spread;
	double report_position_offset;
	double report_order;
	double max_leverage;
	double external_balance;
	unsigned int grant_trade_minutes;

	unsigned int spread_calc_stdev_hours;
	unsigned int spread_calc_sma_hours;

	bool dry_run;
	bool internal_balance;
	bool detect_manual_trades;
	bool enabled;
	bool hidden;
	bool dynmult_sliding;
	bool dynmult_mult;
	bool zigzag;
	bool swap_symbols;

	Strategy strategy = Strategy(nullptr);

	void loadConfig(json::Value data, bool force_dry_run);

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
		std::optional<IStockApi::Order> buy,sell;
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
			PWalletDB walletDB,
			Config config);



	void perform(bool manually);

	void init();
	bool need_init() const;

	OrderPair getOrders();
	void setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<double> &alert);


	using ChartItem = IStatSvc::ChartItem;
	using Chart = std::vector<ChartItem>;


	struct Status {
		IStockApi::Ticker ticker;
		double curPrice;
		double curStep;
		double assetBalance;
		double currencyBalance;
		double currencyUnadjustedBalance;
		double new_fees;
		double spreadCenter;
		IStockApi::TradesSync new_trades;
		ChartItem chartItem;
		std::size_t enable_alerts_after_minutes;
	};

	Status getMarketStatus() const;

	Order calculateOrder(double lastTradePrice,
			double step,
			double dynmult,
			double curPrice,
			double balance,
			double currency,
			double mult,
			const ZigZagLevels &zlev,
			bool alerts) const;
	Order calculateOrderFeeLess(
			double lastTradePrice,
			double step,
			double dynmult,
			double curPrice,
			double balance,
			double currency,
			double mult,
			const ZigZagLevels &zlev,
			bool alerts) const;
	bool calculateOrderFeeLessAdjust(Order &order,
			int dir, double mult, bool alert, double min_size,
			const ZigZagLevels &zlev) const;


	Config getConfig() {return cfg;}

	const IStockApi::MarketInfo &getMarketInfo() const {return minfo;}

	bool eraseTrade(std::string_view id, bool trunc);
	void clearStats();
	void reset(std::optional<double> achieve_pos = std::optional<double>());

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


	static VisRes visualizeSpread(std::function<std::optional<ChartItem>()> &&source, double sma, double stdev, double mult, double dyn_raise, double dyn_fall, json::StrViewA dynMode, bool sliding, bool dyn_mult, bool strip, bool onlyTrades);

	std::optional<double> getInternalBalance() const;
	std::optional<double> getInternalCurrencyBalance() const;


	void saveState();
	void addAcceptLossAlert();

	auto getUID() const {return uid;}
	bool isInitialResetRequired() const {return need_initial_reset;}

protected:
	class DynMultControl {
	public:
		DynMultControl(double raise, double fall, Dynmult_mode mode, bool mult):raise(raise),fall(fall),mode(mode),mult_buy(1),mult_sell(1),mult(mult) {}

		void setMult(double buy, double sell);
		double getBuyMult() const;
		double getSellMult() const;

		double raise_fall(double v, bool israise);
		void update(bool buy_trade,bool sell_trade);
		void reset();

	protected:

		double raise;
		double fall;
		Dynmult_mode mode;
		double mult_buy;
		double mult_sell;
		bool mult;

	};

	PStockApi stock;
	Config cfg;
	IStockApi::MarketInfo minfo;
	StoragePtr storage;
	PStatSvc statsvc;
	PWalletDB walletDB;
	Strategy strategy;
	DynMultControl dynmult;
	bool need_load = true;
	bool recalc = true;
	bool first_cycle = true;
	bool achieve_mode = false;
	bool need_initial_reset = true;
	double lastPriceOffset = 0;
	json::Value test_backup;
	json::Value lastTradeId = nullptr;
	std::optional<double> sell_alert, buy_alert;
	bool need_live_balance;

	using TradeItem = IStockApi::Trade;
	using TWBItem = IStatSvc::TradeRecord;

	std::vector<ChartItem> chart;
	TradeHistory trades;

	std::optional<double> internal_balance;
	std::optional<double> currency_balance;
	std::optional<double> currency_unadjusted_balance;

	size_t magic = 0;
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

	void updateZigzagLevels();
	void modifyOrder(const ZigZagLevels &zlevs, double dir, Order &order) const;

	void checkLeverage(const Order &order) const;
	bool checkLeverage(const Order &order, double &maxSize) const;

	WalletDB::Key getWalletKey() const;
private:
	template<typename Iter>
	static SpreadCalcResult stCalcSpread(Iter beg, Iter end, unsigned int input_sma, unsigned int input_stdev);


	void initialize();
	mutable std::uint64_t period_cache = 0;

	bool checkAchieveModeDone(const Status &st);
	bool checkEquilibriumClose(const Status &st, double lastTradePrice);
};




#endif /* SRC_MAIN_MTRADER_H_ */
