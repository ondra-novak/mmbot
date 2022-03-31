/*
 * backtest2.h
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_BACKTEST2_H_
#define SRC_MAIN_BACKTEST2_H_

#include <shared/refcnt.h>
#include "trader_factory.h"

#include "spreadgenerator.h"

#include "istrategy3.h"

#include "trader.h"

#include "istockapi.h"


class Backtest: public ondra_shared::RefCntObj {
public:

	Backtest(const Trader_Config_Ex &cfg,
					 const IStockApi::MarketInfo &minfo,
					 double assets,
					 double currency);


	class Source;



	void start(std::vector<double> &&prices, std::uint64_t start_time);
	bool next();

	Trader &get_trader();

	class Reporting;


protected:
	Trader_Config_Ex cfg;
	PStockApi source;
	std::unique_ptr<Trader> trader;
	std::vector<double> prices;
	std::size_t pos = 0;
	std::uint64_t start_time = 0;

protected: //reported data;
	ondra_shared::StringView<IStatSvc::TradeRecord> trades;
	IStatSvc::ErrorObj error;
	IStatSvc::MiscData miscData;
	IStatSvc::Info info;
	std::optional<IStockApi::Order> buy;
	std::optional<IStockApi::Order> sell;
	double cur_price = 0;
	double position = 0;



};




#endif /* SRC_MAIN_BACKTEST2_H_ */
