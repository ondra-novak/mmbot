/*
 * strategy_pile.h
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_PILE_H_
#define SRC_MAIN_STRATEGY_PILE_H_

#include <string_view>

#include "istrategy3.h"


class Strategy3_Pile: public IStrategy3 {
public:
	Strategy3_Pile(double ratio);
	Strategy3_Pile(double ratio, double constant);
	virtual ChartPoint get_chart_point(double price) const override;
	virtual json::Value save() const override;
	virtual PStrategy3 run(AbstractTraderControl &cntr) const override;
	virtual PStrategy3 load(const json::Value &state) const override;
	virtual double calc_initial_position(const InitialState &st) const override;
	virtual std::string_view get_id() const override;;

	static std::string_view id;

	virtual PStrategy3 reset() const override;
	static void reg(AbstractStrategyRegister &r);

protected:
	static double calcPos(double r, double c, double price);
	static double calcBudget(double r, double c, double price);
	static double calcAlloc(double r, double c, double price);
	static double calcPriceFromPos(double r, double c, double pos);
	static double calcPriceFromCurrency(double r, double c, double cur);
	static double calcConstant(double r, double price, double eq);

	double ratio = 0;
	double constant = 0;
};

#endif /* SRC_MAIN_STRATEGY_PILE_H_ */
