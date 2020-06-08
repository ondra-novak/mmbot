/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#define SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#include <chrono>

#include "istrategy.h"
#include "strategy_leveraged_base.h"


class Hyperbolic_Calculus {
public:
	///Calculate neutral price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param position current position
	 * @param curPrice current price
	 * @return neutral price for given values
	 */
	static double calcNeutral(double power, double asym, double position, double curPrice);

	static double calcPosValue(double power, double asym, double neutral, double curPrice);
	///Calculate roots of value function (for maximum loss)
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral position
	 * @param balance balance allocate to trade (max loss)
	 * @return two roots which specifies tradable range
	 */
	static IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance);

	///Calculate position for given price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral price
	 * @param price requested price
	 * @return position at given price
	 */
	static double calcPosition(double power, double asym, double neutral, double price);
	///Calculate price, where position is zero
	/**
	 * @param neutral_price neutral price
	 * @param asym asymmetry
	 * @return price where position is zero
	 */
	static double calcPrice0(double neutral_price, double asym);

	//static double calcValue0(double power, double asym, double neutral);

	static double calcNeutralFromPrice0(double price0, double asym);

	static double calcPriceFromPosition(double power, double asym, double neutral, double position);
	///Calculates price from given value
	/**
	 * Position value is calculated using calcPosValue(). This function is inversed version if the function calcPosValue().
	 *
	 * @param power Power multiplier
	 * @param asym asymmetry (-1,1)
	 * @param neutral neutral price
	 * @param value position value
	 * @param curPrice on which price the value was retrieved. It is used to specify which side of the chart will be used, because there
	 * are always two results.
	 *
	 * @return found price. If you need position, call calcPosition on price. If the value is above of maximum value, result is middle price. To
	 * retrieve that value, call calcValue0()
	 */
	//static double calcPriceFromValue(double power, double asym, double neutral, double value, double curPrice);

	static double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);

	static double calcPower(double neutral, double balance, double asym);

	static std::string_view id;
};

using Strategy_Hyperbolic = Strategy_Leveraged<Hyperbolic_Calculus>;

class Linear_Calculus {
public:
	///Calculate neutral price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param position current position
	 * @param curPrice current price
	 * @return neutral price for given values
	 */
	static double calcNeutral(double power, double asym, double position, double curPrice);

	static double calcPosValue(double power, double asym, double neutral, double curPrice);
	///Calculate roots of value function (for maximum loss)
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral position
	 * @param balance balance allocate to trade (max loss)
	 * @return two roots which specifies tradable range
	 */
	static IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance);

	///Calculate position for given price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral price
	 * @param price requested price
	 * @return position at given price
	 */
	static double calcPosition(double power, double asym, double neutral, double price);
	///Calculate price, where position is zero
	/**
	 * @param neutral_price neutral price
	 * @param asym asymmetry
	 * @return price where position is zero
	 */
	static double calcPrice0(double neutral_price, double asym);

	//static double calcValue0(double power, double asym, double neutral);

	static double calcNeutralFromPrice0(double price0, double asym);

	static double calcPriceFromPosition(double power, double asym, double neutral, double position);
	///Calculates price from given value
	/**
	 * Position value is calculated using calcPosValue(). This function is inversed version if the function calcPosValue().
	 *
	 * @param power Power multiplier
	 * @param asym asymmetry (-1,1)
	 * @param neutral neutral price
	 * @param value position value
	 * @param curPrice on which price the value was retrieved. It is used to specify which side of the chart will be used, because there
	 * are always two results.
	 *
	 * @return found price. If you need position, call calcPosition on price. If the value is above of maximum value, result is middle price. To
	 * retrieve that value, call calcValue0()
	 */
	//static double calcPriceFromValue(double power, double asym, double neutral, double value, double curPrice);

	static double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);

	static double calcPower(double neutral, double balance, double asym);

	static std::string_view id;
};

using Strategy_Linear = Strategy_Leveraged<Linear_Calculus>;


#endif /* SRC_MAIN_STRATEGY_HYPERBOLIC_H_ */

