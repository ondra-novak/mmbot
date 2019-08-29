/*
 * backtest.cpp
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */


#include "backtest.h"

#include <random>
#include <chrono>
#include <iomanip>

#include "stats2report.h"

BacktestControl::BacktestControl(IStockSelector &stockSel,
		std::unique_ptr<BtReport> &&rpt, const Config &config,
		ondra_shared::StringView<IStatSvc::ChartItem> chart,
		double balance) {

	prepareChart(config,chart);


	IStockApi *orig_broker = stockSel.getStock(config.broker);
	if (orig_broker == nullptr)
		throw std::runtime_error(std::string("Unknown stock market name: ")+std::string(config.broker));

	auto minfo = orig_broker->getMarketInfo(config.pairsymb);

//	PStatSvc statsvc ( new Stats2Report([=](CalcSpreadFn &&fn) {fn();}, "backtest", rpt, config.calc_spread_minutes));

	class FakeStockSelector: public IStockSelector {
	public:
		virtual IStockApi *getStock(const std::string_view &stockName) const override {
			return api;
		}
		virtual void forEachStock(EnumFn fn) const override {
			fn("backtest_broker",*api);
		}

		FakeStockSelector(IStockApi *api):api(api) {}

	protected:
		IStockApi *api;
	};


	broker.emplace(this->chart, minfo, 0, config.mirror);
	FakeStockSelector fakeStockSell(&(*broker));
	trader.emplace(fakeStockSell, nullptr, std::move(rpt), config);
	trader->setInternalBalance(balance);
}



bool BacktestControl::step() {
	if (!broker->reset()) return false;
	trader->perform();
	return true;
}

BacktestControl::Config BacktestControl::loadConfig(const std::string &fname,
		const std::string &section,
		const std::vector<ondra_shared::IniItem> &custom_options, double spread) {

	ondra_shared::IniConfig cfg;
	cfg.load(fname);
	for (auto &&x: custom_options) {
		cfg.load(x);
	}
	Config c(MTrader::load(cfg[section],true));
	c.enabled = true;
	auto sect = cfg[section];
	c.calc_spread_minutes = sect["spread_calc_interval"].getUInt(0);
	c.mirror = sect["mirror"].getBool(true);
	c.repeat = sect["repeat"].getUInt(0);
	c.trend = sect["trend"].getNumber(0);
	c.random_merge = sect["merge"].getBool(false);
	c.random_mins = sect["random"].getNumber(0);
	if (c.random_mins) {
		c.random_seed = sect.mandatory["seed"].getUInt();
		ondra_shared::StrViewA randoms = sect.mandatory["stddev"].getString();
		auto splt = randoms.split("/");
		while (!!splt) {
			ondra_shared::StrViewA r = splt();
			double rn = strtod(r.data,nullptr);
			c.randoms.push_back(rn);
		}
	}
	c.dump_chart = sect["dump_chart"].getPath();
	c.title="BT:"+c.title;
	if (c.calc_spread_minutes == 0 && c.force_spread == 0) {
		c.force_spread = spread;
	}
	return c;
}

BacktestControl::BtReport::BtReport(PStatSvc &&rpt):rpt(std::move(rpt)) {
}

void BacktestControl::BtReport::reportOrders(
		const std::optional<IStockApi::Order> &buy,
		const std::optional<IStockApi::Order> &sell) {
	this->buy = buy;
	this->sell = sell;
}

void BacktestControl::BtReport::reportTrades(
		ondra_shared::StringView<IStockApi::TradeWithBalance> trades,  double neutral_pos) {
	this->trades = trades;
	this->neutral_pos = neutral_pos;
}

void BacktestControl::BtReport::reportPrice(double price) {
	this->price = price;
}

void BacktestControl::BtReport::setInfo(const Info &info) {
	this->info = info;
}

void BacktestControl::BtReport::reportMisc(const MiscData &miscData) {
	this->miscData = miscData;
}

void BacktestControl::BtReport::reportError(const ErrorObj &e) {
	this->buyError =e.buyError;
	this->sellError = e.sellError;

}

double BacktestControl::BtReport::calcSpread(
		ondra_shared::StringView<ChartItem> chart, const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo, double balance,
		double prev_value) const {
	return rpt->calcSpread(chart,config,minfo,balance,prev_value);
}

std::size_t BacktestControl::BtReport::getHash() const {
	return 1;
}

void BacktestControl::BtReport::flush() {
	rpt->setInfo(info);
	rpt->reportMisc(miscData);
	rpt->reportOrders(buy,sell);
	rpt->reportPrice(price);
	rpt->reportTrades(trades,neutral_pos);
	rpt->reportError(ErrorObj (buyError,sellError));
}

void BacktestControl::prepareChart(const Config &config,
		ondra_shared::StringView<ChartItem> chart) {

	double init_price = 1;
	if (!chart.empty()) init_price = chart[0].last;
	std::vector<double> relchart;
	double beg = init_price;
	double t = exp(config.trend/100.0);
	std::vector<double> diffs(config.randoms.size(),1.0);
	if (config.random_mins) {
		std::default_random_engine rnd(config.random_seed);
		std::vector<std::lognormal_distribution<> > norms;
		for (auto &&c : config.randoms) {
			norms.emplace_back(0.0, c*0.01);
		}
		for (std::size_t i = 0; i < config.random_mins; i++) {
			double diff = 1.0;
			double mdiff = 1.0;
			if (!chart.empty() && config.random_merge) {
				auto &&x = chart[i & chart.length];
				mdiff = x.last/beg;
				beg = x.last;
			}
			for (std::size_t j = 0; j < norms.size(); j++) {
				bool recalc = (relchart.size() % (1 << j) == 0);
				if (recalc) diffs[j] = norms[j](rnd);
				diff *= diffs[j];
			}
			diff *= mdiff;
			relchart.push_back(diff*t);
		}
	} else {
		for (const auto &x : chart) {
			double diff = x.last/beg;
			relchart.push_back(diff*t);
			beg = x.last;
		}
	}

	std::size_t cnt = relchart.size();
	for (std::size_t i = 0; i < config.repeat; i++) {
		for (std::size_t j = 0; j < cnt; j++) {
			relchart.push_back(relchart[j]);
		}
	}

	cnt = relchart.size();
	std::size_t begtime = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	constexpr std::size_t minute = 60000;
	begtime -= cnt * minute;
	this->chart.clear();
	beg = init_price;
	for (auto &&x: relchart) {
		double p = beg * x;
		beg = p;
		this->chart.push_back(ChartItem {
			begtime, p,p,p
		});
		begtime+=minute;
	}

	if (!config.dump_chart.empty()) {
		std::ofstream f(config.dump_chart, std::ios::out|std::ios::trunc);
		if (!f) throw std::runtime_error("Can't open: "+ config.dump_chart);
		for (auto &&x : this->chart) {
			time_t t = x.time/1000;
			f << std::put_time(gmtime(&t), "%FT%TZ") << "," << x.last << std::endl;
		}
	}

}
