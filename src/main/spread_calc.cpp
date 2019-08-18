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
#include "mtrader.h"

using ondra_shared::logInfo;
using ondra_shared::logDebug;

class StockEmulator: public IStockApi {
public:

	StockEmulator(ondra_shared::StringView<IStatSvc::ChartItem> chart,
			const MarketInfo &minfo,
			double balance);
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override ;
	virtual void testBroker() override {}
	virtual double getBalance(const std::string_view &) override;
	virtual bool isTest() const override {return false;}
	virtual MarketInfo getMarketInfo(const std::string_view &) override{
		return minfo;
	}
	virtual double getFees(const std::string_view &) override{
		return minfo.fees;
	}
	virtual std::vector<std::string> getAllPairs() override {return {};}

	double getScore() const {
		return currency+sqrt(chart[0].bid*chart[0].ask)*balance;
	}
	unsigned int getTradeCount() const {
		return std::min(buys,sells);
	}

protected:
	double currency=0;
	ondra_shared::StringView<IStatSvc::ChartItem> chart;
	TradeHistory trades;
	Order buy, sell;
	bool buy_ex = true, sell_ex = true;
	std::size_t pos = 0;
	bool back = false;
	MarketInfo minfo;
	double balance;
	double initial_balance;
	unsigned int buys=0, sells=0;

};

class EmptyStorage: public IStorage {
public:
	virtual void store(json::Value) {};
	virtual json::Value load() {return json::Value();}

};

class EmulStatSvc: public IStatSvc {
public:
	EmulStatSvc(double spread):spread(spread) {}

	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades) override {}
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

protected:
	double spread;
};


struct EmulResult {
	double score;
	int trades;
};

static EmulResult emulateMarket(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double spread) {


	StockEmulator emul(chart, minfo, balance);

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
	cfg.sliding_pos_change = 0;
	cfg.sliding_pos_acm = false;

	class Selector: public IStockSelector {
	public:
		Selector(IStockApi &emul):emul(emul)  {}
		virtual IStockApi *getStock(const std::string_view &) const {return &emul;}
		virtual void forEachStock(EnumFn fn) const {fn("emil", emul);}

	protected:
		IStockApi &emul;
	};


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

	double score = emul.getScore();
	std::intptr_t tcount = emul.getTradeCount();
	if (tcount == 0) return EmulResult{-1001,0};
	std::intptr_t min_count = std::max<std::intptr_t>(counter*cfg.spread_calc_min_trades/1440,1);
	std::intptr_t max_count = (counter*cfg.spread_calc_max_trades+1439)/1440;
	if (tcount < min_count) score = tcount-min_count;
	else if (tcount > max_count) score = max_count-tcount;
	return EmulResult {
		score,
		static_cast<int>(tcount)
	};
}



double glob_calcSpread2(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val) {
	double curprice = sqrt(chart[chart.length-1].ask*chart[chart.length-1].bid);


	using ResultItem = std::pair<double,double>;
	ResultItem bestResults[]={
			{-1000,prev_val},
			{-1000,prev_val},
			{-1000,prev_val},
			{-1000,prev_val}
	};

	double low_spread = curprice*(std::exp(prev_val)-1)/10;
	const int steps = 200;
	double hi_spread = curprice*(std::exp(prev_val)-1)*10;
	auto resend = std::end(bestResults);
	auto resbeg = std::begin(bestResults);
	auto resiter = resbeg;

	for (int i = 0; i < steps; i++) {

		double curSpread = std::log(((low_spread+(hi_spread-low_spread)*i/(steps-1.0))+curprice)/curprice);
		auto res = emulateMarket(chart, config, minfo, balance, curSpread);
		auto profit = res.score;
		ResultItem resitem(profit,curSpread);
		if (resiter->first < resitem.first) {
			*resiter = resitem;
			resiter = std::min_element(resbeg, resend);
			ondra_shared::logDebug("Found better spread= $1 (log=$2), score=$3, trades=$4", curprice*exp(curSpread)-curprice, curSpread, profit, res.trades);
		}
	}
	double sugg_spread = pow(std::accumulate(
			std::begin(bestResults),
			std::end(bestResults), ResultItem(1,1),
			[](const ResultItem &a, const ResultItem &b) {
				return ResultItem(0,a.second * b.second);}).second,1.0/std::distance(resbeg, resend));
	return sugg_spread;
}

double glob_calcSpread(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val) {
	if (prev_val < 1e-10) prev_val = 0.01;
	if (chart.empty() || balance == 0) return prev_val;
	double curprice = sqrt(chart[chart.length-1].ask*chart[chart.length-1].bid);
	double sp1 = glob_calcSpread2(chart, config, minfo, balance, prev_val);
	double sp2 = sp1;
	if (chart.length > 1000) {
		 sp2 = glob_calcSpread2(chart.substr(chart.length-1000), config, minfo, balance, prev_val);
	}
	double sp3 = (sp1 + sp2)/2.0;
	logInfo("Spread calculated: long=$1, short=$2, final=$3",curprice*(exp(sp1)-1),
														     curprice*(exp(sp2)-1),
															 curprice*(exp(sp3)-1));
	return sp3;


}




inline StockEmulator::StockEmulator(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MarketInfo &minfo, double balance)
	:chart(chart),minfo(minfo),balance(balance),initial_balance(balance) {
}

inline StockEmulator::TradeHistory StockEmulator::getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {
	return TradeHistory(trades.begin()+lastId.getUInt(), trades.end());
}


inline StockEmulator::Orders StockEmulator::getOpenOrders(const std::string_view & par) {
	Orders ret;
	if (!buy_ex) {
		ret.push_back(buy);
	}
	if (!sell_ex) {
		ret.push_back(sell);
	}
	return ret;
}

inline StockEmulator::Ticker StockEmulator::getTicker(const std::string_view & piar) {
	return Ticker {
		chart[pos].bid,
		chart[pos].ask,
		std::sqrt(chart[pos].bid*chart[pos].ask),
		chart[pos].time
	};
}

inline json::Value StockEmulator::placeOrder(const std::string_view & ,
		double size, double price,json::Value clientId,
		json::Value ,double ) {

	Order ord{0,clientId, size, price};
	if (size < 0) {
		sell = ord;
		sell_ex = false;
	} else {
		buy = ord;
		buy_ex = false;
	}

	return 1;
}



inline bool StockEmulator::reset() {
	auto nx = pos+(back?-1:1);
	if (chart.length <= nx) {
		if (back)
			return false;
		else {
			back = true;
			return reset();
		}
	}
	pos = nx;
	const IStatSvc::ChartItem &p = chart[pos];

	auto txid = trades.size()+1;

	if (p.bid > sell.price && !sell_ex) {
		Trade tr;
		tr.eff_price = sell.price;
		tr.eff_size = sell.size;
		tr.id = txid;
		tr.price = sell.price;
		tr.size = sell.size;
		tr.time = 0;
		minfo.removeFees(tr.eff_size, tr.eff_price);
		sell_ex = true;
		trades.push_back(tr);
		balance += tr.eff_size;
		currency -= tr.eff_size*tr.eff_price;
		sells++;
	}
	if (p.ask < buy.price && !buy_ex) {
		Trade tr;
		tr.eff_price = buy.price;
		tr.eff_size = buy.size;
		tr.id = txid;
		tr.price = buy.price;
		tr.size = buy.size;
		tr.time = 0;
		minfo.removeFees(tr.eff_size, tr.eff_price);
		buy_ex = true;
		trades.push_back(tr);
		balance += tr.eff_size;
		currency -= tr.eff_size*tr.eff_price;
		buys++;
	}

	return true;

}

double StockEmulator::getBalance(const std::string_view & x) {
	return balance;
}
