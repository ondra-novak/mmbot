/*
 * idailyperfmod.h
 *
 *  Created on: 24. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_IDAILYPERFMOD_H_
#define SRC_MAIN_IDAILYPERFMOD_H_
#include <string>

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

	virtual void sendItem(const PerformanceReport &report) = 0;
	virtual json::Value getReport()  = 0;
	virtual ~IDailyPerfModule() {}
};



#endif /* SRC_MAIN_IDAILYPERFMOD_H_ */
