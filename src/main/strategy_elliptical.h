/*
 * strategy_elliptical.h
 *
 *  Created on: 7. 6. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_ELLIPTICAL_H_
#define SRC_MAIN_STRATEGY_ELLIPTICAL_H_



#include <chrono>

#include "istrategy.h"
#include "strategy_leveraged_base.h"

class Elliptical_Calculus{
public:

	Elliptical_Calculus(double width, bool inverted = false);
	double calcPosValue(double power, double asym, double neutral, double curPrice) const;
	double calcPosition(double power, double asym, double neutral, double price)  const;
	double calcNeutral(double power, double asym, double position, double curPrice)  const;
	double calcPrice0(double neutral_price, double asym)  const;
	double calcPriceFromPosition(double power, double asym, double neutral, double position)  const;
	double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice)  const;
	IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance)  const;
	double calcPower(double neutral, double balance, double asym)  const;

	bool isValid(const IStockApi::MarketInfo &minfo) const;
	Elliptical_Calculus init(const IStockApi::MarketInfo &minfo) const;

	static std::string_view id;

protected:
	double _width;
	bool _inverted;
	double width(double neutral) const;


};

using Strategy_Elliptical = Strategy_Leveraged<Elliptical_Calculus>;


#endif /* SRC_MAIN_STRATEGY_ELLIPTICAL_H_ */
