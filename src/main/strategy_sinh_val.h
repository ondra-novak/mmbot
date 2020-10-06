/*
 * strategy_sinh.h
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */


#include "istrategy.h"
#include "strategy_leveraged_base.h"


class SinhVal_Calculus {
public:
	SinhVal_Calculus(double p, double curv);
	double calcNeutral(double power, double asym, double position, double curPrice);
	double calcPosValue(double power, double asym, double neutral, double curPrice);
	IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance);
	double calcPosition(double power, double asym, double neutral, double price);
	double calcPositionDer1(double power, double asym, double neutral, double price);
	double calcPrice0(double neutral_price, double asym);
	double calcNeutralFromPrice0(double price0, double asym);
	double calcPriceFromPosition(double power, double asym, double neutral, double position);
	double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);
	double calcPower(double neutral, double balance, double asym);
	bool isValid(const IStockApi::MarketInfo &minfo)  {return true;}
	SinhVal_Calculus init(const IStockApi::MarketInfo &minfo) {return SinhVal_Calculus(p,curv);}


	static std::string_view id;
protected:
	double p;
	double curv;
};

using Strategy_SinhVal = Strategy_Leveraged<SinhVal_Calculus>;

