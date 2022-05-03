/*
 * istatsvc.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTATSVC_H_
#define SRC_MAIN_ISTATSVC_H_


#include <shared/stringview.h>
#include "istatsvc.h"

#include <cmath>
#include <memory>
#include <optional>

#include "traderecord.h"
#include "istockapi.h"

struct MTrader_Config;
struct PerformanceReport;
class Strategy;

class IStatSvc {
public:


	struct MiscData {
		int trade_dir;
		bool achieve_mode;
		bool enabled;
		double equilibrium;
		double spread;
		double dynmult_buy;
		double dynmult_sell;
		double lowest_price;
		double highest_price;
		double budget_total;
		double budget_assets;
		double accumulated;
		double budget_extra;
		std::size_t total_trades;
		std::uint64_t total_time;
		double lastTradePrice;
		double position;
		double cur_norm_buy;
		double cur_norm_sell;
		double entry_price;
		double rpnl;
		double upnl;
	};


	struct Info {
		std::string_view title;
		std::string_view brokerName;
		IStockApi::MarketInfo minfo;
		PStockApi exchange;
		double order;
	};

	struct ErrorObj {
		std::string_view genError;
		std::string_view buyError;
		std::string_view sellError;
	};
	struct ErrorObjEx: public ErrorObj {
		explicit ErrorObjEx(const char *what): ErrorObj{what,"",""} {}
		ErrorObjEx(const std::string_view &buy_error,const std::string_view &sell_error)
			: ErrorObj{"",buy_error,sellError} {}
	};


	virtual void reportOrder(int n, double price, double size, double pl, double np) = 0;
	virtual void reportPrice(double price, double pl, double np) = 0;
	virtual void reportTrades(double finalPos, bool inverted, ondra_shared::StringView<TradeRecord> trades) = 0;
	virtual void setInfo(const Info &info) = 0;
	virtual void reportMisc(const MiscData &miscData, bool initial = false) = 0;
	virtual void reportError(const ErrorObj &errorObj) = 0;
	virtual void reportPerformance(const PerformanceReport &repItem) = 0;
	virtual void reportLogMsg(std::uint64_t timestamp, const std::string_view &text) = 0;
	virtual std::size_t getHash() const = 0;

	///inicialize report instance - called once the trader is started and initialized
	virtual void init() = 0;

	virtual ~IStatSvc() {}
};


using PStatSvc = std::unique_ptr<IStatSvc>;

#endif /* SRC_MAIN_ISTATSVC_H_ */
