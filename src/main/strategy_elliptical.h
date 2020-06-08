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

	Elliptical_Calculus(double width);
	double calcPosValue(double power, double asym, double neutral, double curPrice);
	double calcPosition(double power, double asym, double neutral, double price);
	double calcNeutral(double power, double asym, double position, double curPrice);
	double calcPrice0(double neutral_price, double asym);
	double calcPriceFromPosition(double power, double asym, double neutral, double position);
	double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);
	IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance);
	double calcPower(double neutral, double balance, double asym);

	static std::string_view id;

protected:
	double _width;
	double width(double neutral) const;


};

using Strategy_Elliptical = Strategy_Leveraged<Elliptical_Calculus>;


#endif /* SRC_MAIN_STRATEGY_ELLIPTICAL_H_ */
