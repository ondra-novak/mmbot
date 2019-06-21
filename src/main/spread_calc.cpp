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
	virtual bool reset() ;
	virtual double getBalance(const std::string_view &) override;
	virtual bool isTest() const override {return false;}
	virtual MarketInfo getMarketInfo(const std::string_view &) {
		return minfo;
	}
	virtual double getFees(const std::string_view &) {
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

	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades) {}
	virtual void reportOrders(const std::optional<IStockApi::Order> &,
							  const std::optional<IStockApi::Order> &) {}
	virtual void reportPrice(double ) {}
	virtual void setInfo(ondra_shared::StrViewA,ondra_shared::StrViewA,ondra_shared::StrViewA,bool ) {}
	virtual double calcSpread(ondra_shared::StringView<ChartItem> ,
			const MTrader_Config &,
			const IStockApi::MarketInfo &,
			double,
			double ) const {return spread;}

protected:
	double spread;
};


static double emulateMarket(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double spread) {


	StockEmulator emul(chart, minfo, balance);

	MTrader_Config cfg(config);
	cfg.dry_run = false;
	cfg.spread_calc_mins=1;
	cfg.internal_balance = false;

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
	if (tcount == 0) return -1001;
	std::intptr_t min_count = std::max<std::intptr_t>(counter*cfg.spread_calc_min_trades/1440,1);
	std::intptr_t max_count = (counter*cfg.spread_calc_max_trades+1439)/1440;
	if (tcount < min_count) score = tcount-min_count;
	else if (tcount > max_count) score = max_count-tcount;
	ondra_shared::logDebug("Try spread= $1, score=$2, trades=$3 ($4 - $5)", spread, score, tcount, min_count, max_count);
	return score;
}




double glob_calcSpread(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val) {
	if (chart.empty() || balance == 0) return prev_val;
	double curprice = sqrt(chart[chart.length-1].ask*chart[chart.length-1].bid);
	if (prev_val < 1e-20) prev_val = 0.01;


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
		double profit= emulateMarket(chart, config, minfo, balance, curSpread);
		ResultItem resitem(profit,curSpread);
		if (resiter->first < resitem.first) {
			*resiter = resitem;
			resiter = std::min_element(resbeg, resend);
		}
	}
	double sugg_spread = pow(std::accumulate(
			std::begin(bestResults),
			std::end(bestResults), ResultItem(1,1),
			[](const ResultItem &a, const ResultItem &b) {
				return ResultItem(0,a.second * b.second);}).second,1.0/std::distance(resbeg, resend));
	ondra_shared::logInfo("Spread calculated: $1 (log=$2)", curprice*exp(sugg_spread)-curprice, sugg_spread);
	return sugg_spread;
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
