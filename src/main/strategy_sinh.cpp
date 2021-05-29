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
std::string_view Sinh2_Calculus::id = "sinh2";


#include "numerical.h"
Sinh_Calculus::Sinh_Calculus(double p, double curv) :
		p(p*curv),curv(curv) {}


//pos = w * (sinh(p - p * x / k ) + a);
double Sinh_Calculus::calcPosition(double power, double neutral, double price) {
	return power*(std::sinh(p - p * price / neutral) );
}

//val = -integral(p,x)(calcPosition()) = -w * (a * (x - k) + k * (1 - cosh(p - p * x / k)) / p)
double Sinh_Calculus::calcPosValue(double power, double neutral, double curPrice) {
	return -power*(neutral * (1- std::cosh(p - p*curPrice/neutral))/p);
}

//pos = w * (sinh(p - p * x / k ) + a);
//k = (p * x)/(p * asinh(a - pos/w));
double Sinh_Calculus::calcNeutral(double power, double position, double curPrice) {
	double y = position/power;
	return (p * curPrice)/(p + std::asinh( - y));

}

double Sinh_Calculus::calcPrice0(double neutral_price) {
	return neutral_price;
}

double Sinh_Calculus::calcNeutralFromPrice0(double price0) {
	return price0;
}

IStrategy::MinMax Sinh_Calculus::calcRoots(double power, double neutral, double balance) {
	auto fn = [=](double x) {
		return calcPosValue(power, neutral, x) - balance;
	};
	double p0 = calcPrice0(neutral);
	if (p0<0) p0 = 0;
	double mnval = numeric_search_r1(p0, fn);
	double mxval = numeric_search_r2(p0, fn);
	return {mnval, mxval};
}

double Sinh_Calculus::calcPriceFromPosition(double power,	double neutral, double position) {
	double y = position/power;
	return (neutral *  (p + std::asinh(- y)))/p;
}

double Sinh_Calculus::calcNeutralFromValue(double power, double neutral, double value, double curPrice) {

	auto fn = [=](double x) {
		return calcPosValue(power, x, curPrice)-value;
	};

	double pos = calcPosition(power, neutral, curPrice);
	double ret;
	if (pos>0) {
		ret = numeric_search_r2(curPrice, fn);
	} else if (pos<0) {
		ret = numeric_search_r1(curPrice, fn);
	} else {
		ret = neutral;
	}
	return ret;
}

double Sinh_Calculus::calcPower(double neutral, double balance) {
	double f = curv / (std::cosh(curv)-1);
	return balance/(neutral*p)*f;
}

template class Strategy_Leveraged<Sinh_Calculus> ;
template class Strategy_Leveraged<Sinh2_Calculus> ;

Sinh2_Calculus::Sinh2_Calculus(double curv):Sinh_Calculus(1,curv) {
}
