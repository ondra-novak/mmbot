/*
 * backtest.h
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */



#ifndef SRC_MAIN_BACKTEST_H_
#define SRC_MAIN_BACKTEST_H_

#include <optional>

#include "backtest_broker.h"
#include "mtrader.h"


class BacktestControl {
public:

	struct Config: public MTrader::Config {
		Config() {}
		Config(const MTrader::Config &x):MTrader::Config(x) {}
		Config(MTrader::Config &&x):MTrader::Config(std::move(x)) {}

		std::size_t calc_spread_minutes;
		std::size_t repeat;
		std::size_t random_mins;
		std::size_t random_seed;
		std::vector<double> randoms;
		double trend;
		bool mirror;
		bool random_merge;
		std::string dump_chart;

	};

	class BtReport: public IStatSvc {
	public:
		BtReport(PStatSvc &&rpt);

		virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
								  const std::optional<IStockApi::Order> &sell) override;
		virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades) override;
		virtual void reportPrice(double price) override;
		virtual void setInfo(const Info &info) override;
		virtual void reportMisc(const MiscData &miscData) override;
		virtual void reportError(const ErrorObj &errorObj) override;
		virtual double calcSpread(ondra_shared::StringView<ChartItem> chart,
				const MTrader_Config &config,
				const IStockApi::MarketInfo &minfo,
				double balance,
				double prev_value) const override;
		virtual std::size_t getHash() const override;

		///Interacts with report object - call in worker - report object is not MT safe
		void flush();

	protected:
		PStatSvc rpt;

		std::optional<IStockApi::Order> buy;
		std::optional<IStockApi::Order> sell;
		ondra_shared::StringView<IStockApi::TradeWithBalance> trades;
		std::string buyError;
		std::string sellError;
		double price;
		MiscData miscData;
		Info info;

	};


	BacktestControl(IStockSelector &stockSel,
					std::unique_ptr<BtReport> &&rpt,
					const Config &config,
					ondra_shared::StringView<IStatSvc::ChartItem> chart,
					double balance);

	bool step();

	static Config loadConfig(const std::string &fname,
			const std::string &section,
			const std::vector<ondra_shared::IniItem> &custom_options,
			double spread);




protected:

	using ChartItem = IStatSvc::ChartItem;

	std::optional<BacktestBroker> broker;
	std::optional<MTrader> trader;
	std::vector<ChartItem> chart;

	void prepareChart(const Config &config, ondra_shared::StringView<ChartItem> chart);


};



#endif /* SRC_MAIN_BACKTEST_H_ */
