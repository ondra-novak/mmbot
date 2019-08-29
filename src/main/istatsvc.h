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
		double last;
	};

	struct MiscData {
		int trade_dir;
		bool achieve;
		double calc_price;
		double spread;
		double dynmult_buy;
		double dynmult_sell;
		double value;
		double boost;
		double lowest_price;
		double highest_price;
		std::size_t total_trades;
	};


	struct Info {
		std::string_view title;
		std::string_view assetSymb;
		std::string_view currencySymb;
		std::string_view priceSymb;
		bool inverted;
		bool margin;
		bool emulated;
	};

	struct ErrorObj {
		std::string_view genError;
		std::string_view buyError;
		std::string_view sellError;
		explicit ErrorObj(const char *what): genError(what) {}
		ErrorObj(const std::string_view &buy_error,const std::string_view &sell_error)
			: buyError(buy_error),sellError(sell_error) {}

	};

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) = 0;
	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades, double neutral_pos) = 0;
	virtual void reportPrice(double price) = 0;
	virtual void setInfo(const Info &info) = 0;
	virtual void reportMisc(const MiscData &miscData) = 0;
	virtual void reportError(const ErrorObj &errorObj) = 0;
	virtual double calcSpread(ondra_shared::StringView<ChartItem> chart,
			const MTrader_Config &config,
			const IStockApi::MarketInfo &minfo,
			double balance,
			double prev_value) const = 0;
	virtual std::size_t getHash() const = 0;

	virtual ~IStatSvc() {}
};


using PStatSvc = std::unique_ptr<IStatSvc>;

#endif /* SRC_MAIN_ISTATSVC_H_ */
