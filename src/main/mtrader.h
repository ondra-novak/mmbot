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
#include "istatsvc.h"
#include "storage.h"
#include "report.h"

class IStockApi;

struct MTrader_Config {
	std::string pairsymb;
	std::string broker;
	std::string title;

	double buy_mult;
	double sell_mult;
	double buy_step_mult;
	double sell_step_mult;
	double asset_base;

	double dynmult_raise;
	double dynmult_fall;

	unsigned int spread_calc_mins;
	unsigned int spread_calc_min_trades;
	unsigned int spread_calc_max_trades;

	bool dry_run;

	std::size_t start_time;

};


class MTrader {
public:

	using StoragePtr = PStorage;
	using Config = MTrader_Config;


	static Config load(const ondra_shared::IniConfig::Section &ini, bool force_dry_run);


	struct Order: public IStockApi::Order {
		bool isSimilarTo(const Order &other);
		Order(const IStockApi::Order& o):IStockApi::Order(o) {}
		Order() {}
	};

	struct OrderPair {
		std::optional<Order> buy,sell;
	};


	MTrader(IStockSelector &stock_selector,
			StoragePtr &&storage,
			PStatSvc &&statsvc,
			Config config);

	///Returns true, if trade was detected, or false, if not
	int perform();


	OrderPair getOrders();
	bool replaceIfNotSame(std::optional<Order> &orig, Order neworder);


	using ChartItem = IStatSvc::ChartItem;


	struct Status {
		double lastTradePrice;
		double basePrice;
		double curPrice;
		double curStep;
		double assetBalance;
		double new_fees;
		IStockApi::TradeHistory new_trades;
		ChartItem chartItem;
	};

	Status getMarketStatus() const;

	Order calculateBuyOrder(const Status &status, double dynmult, const std::optional<Order> &curOrder) const;
	Order calculateSellOrder(const Status& status, double dynmult, const std::optional<Order> &curOrder) const;

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
		double money_left;
		double assets_left;

	};

	CalcRes calc_min_max_range();

	bool eraseTrade(std::string_view id, bool trunc);

protected:
	std::unique_ptr<IStockApi> ownedStock;
	IStockApi &stock;
	Config cfg;
	IStockApi::MarketInfo minfo;
	StoragePtr storage;
	PStatSvc statsvc;
	bool need_load = true;

	using TradeItem = IStockApi::Trade;

	std::vector<ChartItem> chart;
	IStockApi::TradeHistory trades;

	double buy_dynmult=1.0;
	double sell_dynmult=1.0;
	mutable double prev_spread=0;
	mutable double initial_price = 0;

	static double adjValue(double sz, double step);
	static double adjValueCeil(double sz, double step);
	double adjSize(double sz) const {return adjValueCeil(sz, minfo.asset_step);}
	double adjPrice(double p) const {return adjValue(p, minfo.currency_step);}
	double addFees(double price, double dir) const;
	double removeFees(double price, double dir) const;

	void loadState();
	void saveState();


	double range_max_price(Status st, double &avail_assets);
	double range_min_price(Status st, double &avail_money);

	double raise_fall(double v, bool raise) const;


	static IStockApi &selectStock(IStockSelector &stock_selector, const Config &conf, std::unique_ptr<IStockApi> &ownedStock);
	std::size_t testStartTime;


	void mergeTrades(std::size_t fromPos);

};




#endif /* SRC_MAIN_MTRADER_H_ */
