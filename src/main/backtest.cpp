/*
 * backtest.cpp
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */


#include "backtest.h"
#include "stats2report.h"

BacktestControl::BacktestControl(IStockSelector &stockSel,
		Report &rpt, Config config,
		ondra_shared::StringView<IStatSvc::ChartItem> chart,
		double spread, double balance) {


	IStockApi *orig_broker = stockSel.getStock(config.mtrader_cfg.broker);
	if (orig_broker == nullptr)
		throw std::runtime_error(std::string("Unknown stock market name: ")+std::string(config.mtrader_cfg.broker));

	auto minfo = orig_broker->getMarketInfo(config.mtrader_cfg.pairsymb);
	if (config.calc_spread_minutes == 0 && config.mtrader_cfg.force_spread == 0) {
		config.mtrader_cfg.force_spread = spread;
	}

	PStatSvc statsvc ( new Stats2Report([=](CalcSpreadFn &&fn) {fn();}, "backtest", rpt, config.calc_spread_minutes));

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


	config.mtrader_cfg.title="BT:"+config.mtrader_cfg.title;
	broker.emplace(chart, minfo, 0);
	FakeStockSelector fakeStockSell(&(*broker));
	trader.emplace(fakeStockSell, nullptr, std::move(statsvc), config.mtrader_cfg);
	trader->setInternalBalance(balance);
}



bool BacktestControl::step() {
	if (!broker->reset()) return false;
	trader->perform();
	return true;
}

BacktestControl::Config BacktestControl::loadConfig(const std::string &fname,
		const std::string &section,
		const std::vector<ondra_shared::IniItem> &custom_options) {

	ondra_shared::IniConfig cfg;
	cfg.load(fname);
	for (auto &&x: custom_options) {
		cfg.load(x);
	}
	Config c;
	c.mtrader_cfg = MTrader::load(cfg[section],true);
	c.calc_spread_minutes = cfg[section]["spread_calc_interval"].getUInt(0);
	return c;
}
