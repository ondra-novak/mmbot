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
#include "calculator.h"
#include "istatsvc.h"
#include "storage.h"
#include "report.h"

class IStockApi;

enum class Dynmult_mode {
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
	double external_assets;
	double min_size;

	double dynmult_raise;
	double dynmult_fall;
	Dynmult_mode dynmult_mode;

	double acm_factor_buy;
	double acm_factor_sell;

	unsigned int accept_loss;

	double sliding_pos_hours;
	double sliding_pos_weaken;

	double force_spread;
	double emulated_currency;
	double report_position_offset;


	unsigned int spread_calc_mins;
	unsigned int spread_calc_min_trades;
	unsigned int spread_calc_max_trades;


	enum NeutralPosType {
		disabled,
		assets,
		currency,
		center
	};

	NeutralPosType neutralPosType;
	double neutral_pos;
	double max_pos;


	bool dry_run;
	bool internal_balance;
	bool detect_manual_trades;
	bool enabled;
	bool force_margin;
	bool dust_orders;

	std::size_t start_time;


	void parse_neutral_pos(ondra_shared::StrViewA txt);

};


class MTrader {
public:

	using StoragePtr = PStorage;
	using Config = MTrader_Config;





	static Config load(const ondra_shared::IniConfig::Section &ini, bool force_dry_run);
	static void showConfig(const ondra_shared::IniConfig::Section &ini, bool force_dry_run, std::ostream &out);
	static Config load(const json::Value object, bool force_dry_run);


	struct Order: public IStockApi::Order {
		bool isSimilarTo(const Order &other, double step);
		Order(const IStockApi::Order& o):IStockApi::Order(o) {}
		Order() {}
		Order(double size, double price){
			this->size = size;
			this->price = price;
		}
	};

	struct OrderPair {
		std::optional<Order> buy,sell;
		static OrderPair fromJSON(json::Value json);
		json::Value toJSON() const;
	};


	MTrader(IStockSelector &stock_selector,
			StoragePtr &&storage,
			PStatSvc &&statsvc,
			Config config);


	///Returns true, if trade was detected, or false, if not
	int perform();

	void init();

	OrderPair getOrders();
	void setOrder(std::optional<Order> &orig, Order neworder);
	void setOrderCheckMaxPos(std::optional<Order> &orig, Order neworder, double balance, double neutral);


	using ChartItem = IStatSvc::ChartItem;


	struct Status {
		double curPrice;
		double curStep;
		double assetBalance;
		double internalBalance;
		double new_fees;
		IStockApi::TradeHistory new_trades;
		ChartItem chartItem;
	};

	Status getMarketStatus() const;


	/// Calculate order
	/**
	 * @param step precalculated step (spread), negative for sell, positive for buy
	 * @param oldPrice price of last trade (reference price)
	 * @param curPrice current price (center price)
	 * @param balance current balance (including external)
	 * @return order
	 */
	Order calculateOrder(double lastTradePrice,
			double step,
			double curPrice,
			double balance,
			double acm,
			double mult) const;
	Order calculateOrderFeeLess(
			double lastTradePrice,
			double step,
			double curPrice,
			double balance,
			double acm,
			double mult) const;

	const Config &getConfig() {return cfg;}

	const IStockApi::MarketInfo getMarketInfo() const {return minfo;}

	struct CalcRes {
		double assets;
		double avail_assets;
		double value;
		double avail_money;
		double min_price;
		double max_price;
		double cur_price;

	};

	CalcRes calc_min_max_range();

	bool eraseTrade(std::string_view id, bool trunc);
	void reset();
	void repair();
	void achieve_balance(double price, double balance);
	ondra_shared::StringView<IStatSvc::ChartItem> getChart() const;
	double getLastSpread() const;
	double getInternalBalance() const;
	void setInternalBalance(double v);
	void dropState();

	static std::string_view vtradePrefix;

protected:
	std::unique_ptr<IStockApi> ownedStock;
	IStockApi &stock;
	Config cfg;
	IStockApi::MarketInfo minfo;
	StoragePtr storage;
	PStatSvc statsvc;
	OrderPair lastOrders[2];
	bool need_load = true;
	bool first_order = true;
	Calculator calculator;

	using TradeItem = IStockApi::Trade;
	using TWBItem = IStockApi::TradeWithBalance;

	std::vector<ChartItem> chart;
	IStockApi::TWBHistory trades;

	double buy_dynmult=1.0;
	double sell_dynmult=1.0;
	double internal_balance = 0;
	mutable double prev_spread=0.01;
	double prev_calc_ref = 0;
	double currency_balance_cache = 0;
	size_t magic = 0;

	void loadState();
	void saveState();


	double range_max_price(Status st, double &avail_assets);
	double range_min_price(Status st, double &avail_money);

	double raise_fall(double v, bool raise) const;


	static IStockApi &selectStock(IStockSelector &stock_selector, const Config &conf, std::unique_ptr<IStockApi> &ownedStock);
	std::size_t testStartTime;

	struct PTResult {
		bool manual_trades;
	};
	PTResult processTrades(Status &st,bool first_trade);

	void mergeTrades(std::size_t fromPos);

	void update_dynmult(bool buy_trade,bool sell_trade);

	bool acceptLoss(std::optional<Order> &orig, const Order &order, const Status &st, double neutral_pos);
	json::Value getTradeLastId() const;


	double calcWeakenMult(double neutral_pos, double balance);
};




#endif /* SRC_MAIN_MTRADER_H_ */
