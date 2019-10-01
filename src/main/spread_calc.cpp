/*
 * spread_calc.cpp
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */


#include "spread_calc.h"

#include <memory>
#include <numeric>

#include "../shared/logOutput.h"
#include "../shared/worker.h"
#include "mtrader.h"
#include "backtest_broker.h"

using ondra_shared::logInfo;
using ondra_shared::logDebug;

using StockEmulator = BacktestBroker;

class EmptyStorage: public IStorage {
public:
	virtual void store(json::Value) {};
	virtual json::Value load() {return json::Value();}

};

class EmulStatSvc: public IStatSvc {
public:
	EmulStatSvc(double spread):spread(spread) {}

	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> , bool) override {}
	virtual void reportOrders(const std::optional<IStockApi::Order> &,
							  const std::optional<IStockApi::Order> &)override  {}
	virtual void reportPrice(double ) override {}
	virtual void reportError(const ErrorObj &) override {}
	virtual void reportMisc(const MiscData &) override {}
	virtual void setInfo(const Info &) override {}
	virtual double calcSpread(ondra_shared::StringView<ChartItem> ,
			const MTrader_Config &,
			const IStockApi::MarketInfo &,
			double,
			double ) const override {return spread;}
	virtual std::size_t getHash() const override {
		return 0xABCDEF;
	}
	virtual void clear() override {}

protected:
	double spread;
};


struct EmulResult {
	double score;
	int trades;
	bool mincount;
	bool maxcount;
};

static EmulResult emulateMarket(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double spread) {


	StockEmulator emul(chart, minfo, balance, true);

	MTrader_Config cfg(config);
	cfg.dry_run = false;
	cfg.spread_calc_mins=1;
	cfg.internal_balance = false;
	cfg.dynmult_fall = 100;
	cfg.dynmult_raise = 0;
	cfg.sell_step_mult = 1;
	cfg.buy_step_mult = 1;
	cfg.acm_factor_buy = 0;
	cfg.acm_factor_sell = 0;
	cfg.accept_loss = 0;
	cfg.max_pos = 0;
	cfg.neutralPosType = MTrader_Config::disabled;
	cfg.sliding_pos_hours=0;
	cfg.sliding_pos_fade=0;
	cfg.sliding_pos_weaken=0;
	cfg.expected_trend = 0;

	class Selector: public IStockSelector {
	public:
		Selector(IStockApi &emul):emul(emul)  {}
		virtual IStockApi *getStock(const std::string_view &) const {return &emul;}
		virtual void forEachStock(EnumFn fn) const {fn("emil", emul);}

	protected:
		IStockApi &emul;
	};

	double initScore = emul.getScore();

	Selector selector(emul);
	std::size_t counter = 1;
	{
		ondra_shared::PLogProvider nullprovider (std::make_unique<ondra_shared::NullLogProvider>());
		ondra_shared::LogObject nullLog(*nullprovider,"");
		ondra_shared::LogObject::Swap swp(nullLog);

		MTrader trader(selector, nullptr,std::make_unique<EmulStatSvc>(spread),cfg);

		trader.perform();
		while (emul.reset()) {
			trader.perform();
			counter++;
		}

	}

	double score = emul.getScore()-initScore;
	std::intptr_t tcount = emul.getTradeCount();
	if (tcount == 0) return EmulResult{-1001,0};
	std::intptr_t min_count = std::max<std::intptr_t>(counter*cfg.spread_calc_min_trades/1440,1);
	std::intptr_t max_count = (counter*cfg.spread_calc_max_trades+1439)/1440;
	bool minr = tcount < min_count;
	bool maxr = tcount > max_count;
	if (minr) score = tcount-min_count;
	else if (maxr) score = max_count-tcount;
	return EmulResult {
		score,
		static_cast<int>(tcount),
		minr,
		maxr
	};
}



std::pair<double,double> glob_calcSpread2(
		ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val) {
	double curprice = sqrt(chart[chart.length-1].ask*chart[chart.length-1].bid);


	using ResultItem = std::pair<double,double>;
	ResultItem bestResults[]={
			{-10,prev_val},
			{-10,prev_val},
			{-10,prev_val},
			{-10,prev_val}
	};

	double low_spread = curprice*(std::exp(prev_val)-1)/10;
	const int steps = 200;
	double hi_spread = curprice*(std::exp(prev_val)-1)*10;
	double best_profit = 0;
	auto resend = std::end(bestResults);
	auto resbeg = std::begin(bestResults);
	auto resiter = resbeg;

	bool mincount = true;
	bool maxcount = true;

	using namespace ondra_shared;

	for (int i = 0; i < steps ; i++) {

		double curSpread = std::log(((low_spread+(hi_spread-low_spread)*i/(steps-1.0))+curprice)/curprice);
			{
				auto res = emulateMarket(chart, config, minfo, balance, curSpread);
				auto profit = res.score;
				mincount = mincount && res.mincount;
				maxcount = maxcount && res.maxcount;
				ResultItem resitem(profit,curSpread);

				if (resiter->first < resitem.first) {
					*resiter = resitem;
					resiter = std::min_element(resbeg, resend);
					ondra_shared::logDebug("Found better spread= $1 (log=$2), score=$3, trades=$4", curprice*exp(curSpread)-curprice, curSpread, profit, res.trades);
					best_profit = profit;
				}
			}
	}

	if (best_profit<=0) {
		if (mincount) return {prev_val/2, 0};
		if (maxcount) return {prev_val*2, 0};
	}

	double sugg_spread = pow(std::accumulate(
			std::begin(bestResults),
			std::end(bestResults), ResultItem(1,1),
			[](const ResultItem &a, const ResultItem &b) {
				return ResultItem(0,a.second * b.second);}).second,1.0/std::distance(resbeg, resend));
	return {sugg_spread,best_profit};
}

double glob_calcSpread(
		ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val) {
	if (prev_val < 1e-10) prev_val = 0.01;
	if (chart.empty() || balance == 0 || !config.enabled) return prev_val;
	double curprice = sqrt(chart[chart.length-1].ask*chart[chart.length-1].bid);
	auto sp1 = glob_calcSpread2(chart, config, minfo, balance, prev_val);
	auto sp2 = sp1;
	if (chart.length > 1000) {
		 sp2 = glob_calcSpread2(chart.substr(chart.length-1000), config, minfo, balance, prev_val);
	}
	double sp3 = (sp1.first + sp2.first)/2.0;
	logInfo("Spread calculated: long=$1 (profit=$2), short=$3 (profit=$4), final=$5",curprice*(exp(sp1.first)-1),
															sp1.second,
														     curprice*(exp(sp2.first)-1),
															 sp2.second,
															 curprice*(exp(sp3)-1));
	return sp3;


}



