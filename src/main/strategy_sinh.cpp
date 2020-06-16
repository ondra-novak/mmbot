/*
 * strategy_sinh.cpp
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */


#include "strategy_sinh.h"
#include "strategy_leveraged_base.tcc"

#include <cmath>

// https://www.desmos.com/calculator/r2qiezykq8

std::string_view Sinh_Calculus::id = "sinh";

#include "numerical.h"
Sinh_Calculus::Sinh_Calculus(double p) :
		p(p) {}


//pos = w * (sinh(p - p * x / k ) + a);
double Sinh_Calculus::calcPosition(double power, double asym, double neutral, double price) {
	return power*(std::sinh(p - p * price / neutral) + asym);
}

//val = -integral(p,x)(calcPosition()) = -w * (a * (x - k) + k * (1 - cosh(p - p * x / k)) / p)
double Sinh_Calculus::calcPosValue(double power, double asym, double neutral, double curPrice) {
	return -power*(asym*(curPrice-neutral)+neutral * (1- std::cosh(p - p*curPrice/neutral))/p);
}

//pos = w * (sinh(p - p * x / k ) + a);
//k = (p * x)/(p * asinh(a - pos/w));
double Sinh_Calculus::calcNeutral(double power, double asym, double position, double curPrice) {
	double y = position/power;
	return (p * curPrice)/(p + std::asinh(asym - y));

}

double Sinh_Calculus::calcPrice0(double neutral_price, double asym) {
	return (neutral_price * (p + std::asinh(asym)))/p;
}

double Sinh_Calculus::calcNeutralFromPrice0(double price0, double asym) {
	return (p * price0)/(p + std::asinh(asym));
}

IStrategy::MinMax Sinh_Calculus::calcRoots(double power, double asym, double neutral, double balance) {
	auto fn = [=](double x) {
		return calcPosValue(power, asym, neutral, x) - balance;
	};
	double p0 = calcPrice0(neutral, asym);
	double mnval = numeric_search_r1(p0, fn);
	double mxval = numeric_search_r2(p0, fn);
	return {mnval, mxval};
}

double Sinh_Calculus::calcPriceFromPosition(double power, double asym,	double neutral, double position) {
	double y = position/power;
	return (neutral *  (p + std::asinh(asym - y)))/p;
}

double Sinh_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
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

double Sinh_Calculus::calcPower(double neutral, double balance, double ) {
	return balance/neutral;
}

template class Strategy_Leveraged<Sinh_Calculus>;
