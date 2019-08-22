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

	struct Config {
		MTrader::Config mtrader_cfg;
		std::size_t calc_spread_minutes;
	};

	BacktestControl(IStockSelector &stockSel,
					Report &rpt,
					Config config,
					ondra_shared::StringView<IStatSvc::ChartItem> chart,
					double spread,
					double balance);

	bool step();

	static Config loadConfig(const std::string &fname,
			const std::string &section,
			const std::vector<ondra_shared::IniItem> &custom_options);




protected:

	std::optional<BacktestBroker> broker;


	std::optional<MTrader> trader;


};



#endif /* SRC_MAIN_BACKTEST_H_ */
