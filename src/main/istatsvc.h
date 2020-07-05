/*
 * istatsvc.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTATSVC_H_
#define SRC_MAIN_ISTATSVC_H_

#include "istatsvc.h"
#include <memory>
#include <optional>

#include "istockapi.h"

struct MTrader_Config;
struct PerformanceReport;
class Strategy;

class IStatSvc {
public:

	struct ChartItem {
		std::uint64_t time;
		double ask;
		double bid;
		double last;
	};

	struct MiscData {
		int trade_dir;
		double calc_price;
		double spread;
		double dynmult_buy;
		double dynmult_sell;
		double lowest_price;
		double highest_price;
		double budget_total;
		double budget_assets;
		std::size_t total_trades;
		std::uint64_t total_time;
	};


	struct Info {
		std::string_view title;
		std::string_view assetSymb;
		std::string_view currencySymb;
		std::string_view priceSymb;
		std::string_view brokerIcon;
		double position_offset;
		double order;
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

	struct TradeRecord: public IStockApi::Trade {

		double norm_profit;
		double norm_accum;
		double neutral_price;

		TradeRecord(const IStockApi::Trade &t, double norm_profit, double norm_accum, double neutral_price)
			:IStockApi::Trade(t),norm_profit(norm_profit),norm_accum(norm_accum),neutral_price(neutral_price) {}

	    static TradeRecord fromJSON(json::Value v) {
	    	return TradeRecord(IStockApi::Trade::fromJSON(v), v["np"].getNumber(), v["ap"].getNumber(), v["p0"].getNumber());
	    }
	    json::Value toJSON() const {
	    	return IStockApi::Trade::toJSON().merge(json::Value(json::object,{
	    			json::Value("np",norm_profit),
					json::Value("ap",norm_accum),
					json::Value("p0",neutral_price)
	    	}));
	    }


	};

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) = 0;
	virtual void reportTrades(ondra_shared::StringView<TradeRecord> trades) = 0;
	virtual void reportPrice(double price) = 0;
	virtual void setInfo(const Info &info) = 0;
	virtual void reportMisc(const MiscData &miscData) = 0;
	virtual void reportError(const ErrorObj &errorObj) = 0;
	virtual void reportPerformance(const PerformanceReport &repItem) = 0;
	virtual std::size_t getHash() const = 0;
	virtual void clear() = 0;

	virtual ~IStatSvc() {}
};


using PStatSvc = std::unique_ptr<IStatSvc>;

#endif /* SRC_MAIN_ISTATSVC_H_ */
