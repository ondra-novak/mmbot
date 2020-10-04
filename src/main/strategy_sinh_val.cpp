/*
 * strategy_sinh.cpp
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */


#include "strategy_sinh_val.h"
#include "strategy_leveraged_base.tcc"

#include <cmath>

// https://www.desmos.com/calculator/r2qiezykq8

std::string_view SinhVal_Calculus::id = "sinh_val";
static const unsigned int int_steps = 10;

#include "numerical.h"
SinhVal_Calculus::SinhVal_Calculus(double p, double curv) :
		p(p*curv),curv(curv) {}


double SinhVal_Calculus::calcPosition(double power, double asym, double neutral, double price) {
	double res = neutral*power*(std::sinh(p - p * price / neutral))/price;
	return res;
}


double SinhVal_Calculus::calcPosValue(double power, double asym, double neutral, double curPrice) {
	double res = -numeric_integral([&](double x){
		return calcPosition(power,asym, neutral, x);
	}, neutral, curPrice,int_steps);
	return res;

}

double SinhVal_Calculus::calcNeutral(double power, double asym, double position, double curPrice) {
	if (position == 0) return curPrice;

	auto fn = [&](double x) {
		return calcPosition(power, asym, x, curPrice) - position;
	};
	double initp = fn(curPrice);
	double res;
	if (initp > 0) {
		res = numeric_search_r1(curPrice, std::move(fn));
	} else if (initp < 0) {
		res = numeric_search_r2(curPrice, std::move(fn));
	} else {
		res = curPrice;
	}
	return res;

}

double SinhVal_Calculus::calcPrice0(double neutral_price, double asym) {
	return neutral_price;
}

double SinhVal_Calculus::calcNeutralFromPrice0(double price0, double asym) {
	return price0;
}

IStrategy::MinMax SinhVal_Calculus::calcRoots(double power, double asym, double neutral, double balance) {
	if (balance < 0) return IStrategy::MinMax {neutral,neutral};
	//position is most of cases less then postion value, so point where price is equal to balance is our limit

	auto fn = [=](double x) {
		return calcPosValue(power, asym, neutral, x) - balance;
	};
	double p0 = calcPrice0(neutral, asym);
	if (p0<0) p0 = 0;
	double mnval = numeric_search_r1(p0, fn);
	double mxval = numeric_search_r2(p0, fn);
	return {mnval, mxval};
}

double SinhVal_Calculus::calcPriceFromPosition(double power, double asym,	double neutral, double position) {
	auto fn = [&](double x) {
		return calcPosition(power, asym, neutral, x) - position;
	};
	double z;
	if (position >0) {
		z = numeric_search_r1(neutral,std::move(fn));
	} else if (position < 0){
		z = numeric_search_r2(neutral,std::move(fn));
	} else {
		z = neutral;
	}
	return z;
}

double SinhVal_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
	auto dFn = [=](double x) {
			double arg = p * (1 - curPrice / x);
			double px = p * x;
			return -power * (asym * px + p * curPrice * std::sinh(arg) + x * std::cosh(arg) - x) / px;
		};
	auto vFn = [=](double x) {
		return calcPosValue(power, asym, x, curPrice) - value;
	};
	double dVal = dFn(curPrice);
	double n0;
	if (dVal > 0) n0 = numeric_search_r2(curPrice, dFn);
	else if (dVal < 0) n0 = numeric_search_r1(curPrice, dFn);
	else n0 = curPrice;

	double ref = vFn(n0);
	if (ref > 0) return neutral;

	double n1 = numeric_search_r1(n0,vFn);
	double n2 = numeric_search_r2(n0,vFn);
	double nn = std::abs(n1-neutral) < std::abs(n2 - neutral)?n1:n2;
	return nn;
}

double SinhVal_Calculus::calcPower(double neutral, double balance, double ) {
	double f = 1/sinh(curv);
	return balance/(neutral*p)*f;
}

template class Strategy_Leveraged<SinhVal_Calculus> ;
