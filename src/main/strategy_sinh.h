/*
 * strategy_sinh.h
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */


#include "istrategy.h"
#include "strategy_leveraged_base.h"


class Sinh_Calculus {
public:
	Sinh_Calculus(double p, double curv);
	double calcNeutral(double power, double asym, double position, double curPrice);
	double calcPosValue(double power, double asym, double neutral, double curPrice);
	IStrategy::MinMax calcRoots(double power, double asym, double neutral, double balance);
	double calcPosition(double power, double asym, double neutral, double price);
	double calcPrice0(double neutral_price, double asym);
	double calcNeutralFromPrice0(double price0, double asym);
	double calcPriceFromPosition(double power, double asym, double neutral, double position);
	double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);
	double calcPower(double neutral, double balance, double asym);
	bool isValid(const IStockApi::MarketInfo &minfo)  {return true;}
	Sinh_Calculus init(const IStockApi::MarketInfo &minfo) {return Sinh_Calculus(p,curv);}


	static std::string_view id;
protected:
	double p;
	double curv;
};

class Sinh2_Calculus: public Sinh_Calculus {
public:
	static std::string_view id;
	Sinh2_Calculus(double curv);
	Sinh2_Calculus init(const IStockApi::MarketInfo &minfo) {return Sinh2_Calculus(curv);}
};


using Strategy_Sinh = Strategy_Leveraged<Sinh_Calculus>;
using Strategy_Sinh2 = Strategy_Leveraged<Sinh2_Calculus>;

