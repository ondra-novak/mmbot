/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPVALUE_H_
#define SRC_MAIN_STRATEGY_KEEPVALUE_H_
#include <chrono>

#include "istrategy3.h"

class Strategy3_KeepValue: public IStrategy3 {
public:

	struct State {
		double k = 0;
		double c = 0;
	};

	Strategy3_KeepValue();
	Strategy3_KeepValue(const State &k);
	virtual ChartPoint get_chart_point(double price) const override;
	virtual json::Value save() const override;
	virtual PStrategy3 run(AbstractTraderControl &cntr) const override;
	virtual PStrategy3 load(const json::Value &state) const override;
	virtual double calc_initial_position(const InitialState &st) const override;
	virtual std::string_view get_id() const override {return id;}

	static std::string_view id;

	virtual PStrategy3 reset() const override;
	static void reg(AbstractStrategyRegister &r);

protected:
	static double calcPos(const State &st, double price);
	static double calcBudget(const State &st, double price);
	static double calcAlloc(const State &st, double price);
	static double calcPriceFromPos(const State &st, double pos);
	static double calcPriceFromCurrency(const State &st, double cur);
	static double calcPriceFromEquity(const State &st, double equity);
	static State calcConstant(double price, double eq, double position);

	State st;
};



#endif /* SRC_MAIN_STRATEGY_KEEPVALUE_H_ */
