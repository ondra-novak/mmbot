/*
 * istatsvc.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTATSVC_H_
#define SRC_MAIN_ISTATSVC_H_

#include <memory>

#include "istockapi.h"

struct MTrader_Config;

class IStatSvc {
public:

	struct ChartItem {
		std::uintptr_t time;
		double ask;
		double bid;
	};

	struct MiscData {
		int trade_dir;
		bool achieve;
		double spread;
		double dynmult_buy;
		double dynmult_sell;
		double value;
		double boost;
		double lowest_price;
		double highest_price;
		std::size_t total_trades;
	};

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) = 0;
	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades) = 0;
	virtual void reportPrice(double price) = 0;
	virtual void setInfo(ondra_shared::StrViewA title,
			ondra_shared::StrViewA assetSymb,
			ondra_shared::StrViewA currencySymb,
			bool emulated) = 0;
	virtual void reportMisc(const MiscData &miscData) = 0;
	virtual double calcSpread(ondra_shared::StringView<ChartItem> chart,
			const MTrader_Config &config,
			const IStockApi::MarketInfo &minfo,
			double balance,
			double prev_value) const = 0;

};


using PStatSvc = std::unique_ptr<IStatSvc>;

#endif /* SRC_MAIN_ISTATSVC_H_ */
