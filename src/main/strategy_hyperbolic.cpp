/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

//https://www.desmos.com/calculator/eyzyduumon

#include "strategy_hyperbolic.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>
#include "numerical.h"

#include "sgn.h"

#include "strategy_leveraged_base.tcc"

std::string_view Hyperbolic_Calculus::id = "hyperbolic";
std::string_view Linear_Calculus::id = "linear";

template class Strategy_Leveraged<Hyperbolic_Calculus>;
template class Strategy_Leveraged<Linear_Calculus>;



double Hyperbolic_Calculus::calcNeutral(double power, double asym, double position, double curPrice) {
	return curPrice * (std::exp(-asym) + position/power);
}

double Hyperbolic_Calculus::calcPosValue(double power, double asym, double neutral, double curPrice) {
	return power * (std::exp(-asym)*(curPrice - neutral) - neutral*std::log(curPrice/neutral));
}


IStrategy::MinMax Hyperbolic_Calculus::calcRoots(double power, double asym, double neutral, double balance) {
	auto fncalc = [&](double x) {
		return calcPosValue(power,asym, neutral, x) - balance;
	};
	double m = calcPrice0(neutral, asym);
	double r1 = numeric_search_r1(m, fncalc);
	double r2 = numeric_search_r2(m, fncalc);
	return {r1,r2};
}

double Hyperbolic_Calculus::calcPosition(double power, double asym, double neutral, double price) {
	return (neutral/price - std::exp(-asym)) * power;
}

double Hyperbolic_Calculus::calcPrice0(double neutral_price, double asym) {
	double x = neutral_price*std::exp(asym);
	return x;
}

double Hyperbolic_Calculus::calcPriceFromPosition(double power, double asym, double neutral, double position) {
	double ea = std::exp(asym);
	return (ea*neutral * power)/(ea *  position + power);
}

double Hyperbolic_Calculus::calcNeutralFromPrice0(double price0, double asym) {
	return price0 / std::exp(asym);
}

double Hyperbolic_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
	//m = maximum of calcPosValue for neutral
	//m = root of  derivation of calcPosValue for neutral
	//m = e^a * curPrice
	//because calcPosValue has two roots, roots are searched from the vertex left end right
	auto m = std::exp(-asym)*curPrice;

	auto fncalc = [&](double x) {
		double v = calcPosValue(power, asym, x, curPrice);
		return v - value;
	};

	double ref = fncalc(m);
	if (ref > 0) return neutral;

	double n1 = numeric_search_r1(m, fncalc);
	double n2 = numeric_search_r2(m, fncalc);
	double n = std::abs(n1 - neutral) < std::abs(n2 - neutral)?n1:n2;
	return n;

}

double Linear_Calculus::calcPosValue(double power, double asym, double neutral, 	double curPrice) {
	return power * (neutral - curPrice)*(neutral + 2*asym*neutral - curPrice)/(2*neutral);
}

IStrategy::MinMax Linear_Calculus::calcRoots(double power, double asym, double neutral, double balance) {
	double s = balance/power;
	double k = neutral;
	double a = asym;
	double sq = sqrt(k)*sqrt(a*a*k + 2*s);
	double rest = k * (1+a);
	double x1 = std::max(0.0,-sq+rest);
	double x2 = std::max(0.0,sq+rest);
	return {std::min(x1,x2),std::max(x1,x2)};
}

double Linear_Calculus::calcNeutral(double power, double asym, double position, double price) {
	return price / (asym + 1 - position/power);
}

double Linear_Calculus::calcPosition(double power, double asym, double neutral, double price) {
	return -power*(price/neutral - 1 - asym);
}

double Linear_Calculus::calcPrice0(double neutral, double asym) {
	return neutral*(asym + 1);

}

double Linear_Calculus::calcNeutralFromPrice0(double price0, double asym) {
	return price0/(asym + 1);
}

double Linear_Calculus::calcPriceFromPosition(double power, double asym, double neutral, double position) {
	return neutral*(asym - position/power + 1);
}

double Linear_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
	double s = value / power;
	double a = asym;
	double p = curPrice;
	double sq = std::sqrt(a*a*p*p + 2*a*p*s + 2*p*s + s*s);
	if (!std::isfinite(sq)) return neutral;
	double cmn = a*p  +p+ s;
	double dnm = 2*a + 1;
	double x1 = (-sq+cmn)/dnm;
	double x2 = (sq+cmn)/dnm;
	double x = (std::abs(neutral - x1) <std::abs(neutral-x2))?x1:x2;
	return x;

}

double Hyperbolic_Calculus::calcPower(double neutral, double balance, double ) {
	return balance/neutral;
}

double Linear_Calculus::calcPower(double neutral, double balance, double ) {
	return balance/neutral;
}
