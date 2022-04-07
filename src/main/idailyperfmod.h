/*
 * idailyperfmod.h
 *
 *  Created on: 24. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_IDAILYPERFMOD_H_
#define SRC_MAIN_IDAILYPERFMOD_H_
#include <string>
#include <optional>
#include <imtjson/value.h>
#include <shared/shared_object.h>

	struct PerformanceReport {
		std::size_t magic;
		std::size_t uid;
		std::string tradeId;
		std::string currency;
		std::string asset;
		std::string broker;
		///price where trade happened
		double price;
		///size of the trade
		double size;
		///account value change (assets are recalculated by current price) (deprecated)
		double change;
		///absolute position
		double position;
		///set true, if the record is from simulator
		bool simulator;
		///set true, if the price is inverted
		bool invert_price;
		///time of execution
		uint64_t time;
		///pnl calculated by acb - from entry price
		double acb_pnl;
	};


class IDailyPerfModule {
public:

	struct QueryParams {
		std::string asset; ///<search for asset (empty to ignore)
		std::string currency; ///<search for currency (empty to ignore)
		std::string broker;  ///<search for broker(empty to ignore)
		std::uint64_t start_date; ///start date in milliseconds, but rounded to nearest day
		std::uint64_t end_date;  ///end date in milliseconds, but rounded to nearest day
		std::uint64_t cursor;   ///cursor, set to continue from last point, 0 default
		unsigned int year;   ///select year, zero to disable
		unsigned int month;	  ///seletc month, zero to disable
		unsigned int limit;   ///count of records in single query
		std::optional<std::uint64_t> uid;    ///filter by uid
		std::optional<std::uint64_t> magic;  ///filter by magic
		bool skip_deleted; ///set true to skip deleted records
		bool aggregate; ///aggregate results on daily basis
	};

	struct QueryResult {
		bool complete;
		std::uint64_t cursor;
		json::Value result;
	};

	struct TradeLocation {
		std::uint64_t cursor;
		std::uint64_t time;
		std::uint64_t uid;
		std::uint64_t magic;
	};


	virtual void sendItem(const PerformanceReport &report) = 0;
	virtual json::Value getReport()  = 0;
	virtual bool querySupported()= 0;
	virtual QueryResult query(const QueryParams &param)= 0;
	virtual json::Value getOptions() = 0;
	virtual json::Value getTraders() = 0;
	virtual void setTradeDeleted(const TradeLocation &loc, bool deleted) = 0;
	virtual bool setTradeDeletedSupported() = 0;
	virtual ~IDailyPerfModule() {}
};

using PPerfModule = ondra_shared::SharedObject<IDailyPerfModule, ondra_shared::SharedObjectVirtualTraits<IDailyPerfModule> >;

#endif /* SRC_MAIN_IDAILYPERFMOD_H_ */
