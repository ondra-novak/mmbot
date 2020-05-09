/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_hyperbolic.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

#include "sgn.h"

#include "strategy_leveraged_base.tcc"

std::string_view Hyperbolic_Calculus::id = "hyperbolic";
std::string_view Linear_Calculus::id = "linear";

template class Strategy_Leveraged<Hyperbolic_Calculus>;
template class Strategy_Leveraged<Linear_Calculus>;

namespace {

const double accuracy = 1e-5;

}


template<typename Fn>
static double numeric_search_r1(double middle, Fn &&fn) {
	double min = 0;
	double max = middle;
	double ref = fn(middle);
	if (ref == 0) return middle;
	double md = (min+max)/2;
	while (md > accuracy && (max - min) / md > accuracy) {
		double v = fn(md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return md;

}

template<typename Fn>
static double numeric_search_r2(double middle, Fn &&fn) {
	double min = 0;
	double max = 1.0/middle;
	double ref = fn(middle);
	if (ref == 0) return middle;
	double md = (min+max)/2;
	while (md * (1.0 / min - 1.0 / max) > accuracy) {
		double v = fn(1.0/md);
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return 1.0/md;

}


double Hyperbolic_Calculus::calcNeutral(double power, double asym, double position, double curPrice) {
	return (curPrice * (position + power - power *  asym))/power;
}

double Hyperbolic_Calculus::calcPosValue(double power, double asym, double neutral, double curPrice) {
	return power * ((asym - 1) * (neutral - curPrice) + neutral * (log(neutral) - log(curPrice)));
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
	return (neutral/price - 1 + asym) * power;
}

double Hyperbolic_Calculus::calcPrice0(double neutral_price, double asym) {
	double x = neutral_price/(1 -  asym);
	if (!std::isfinite(x)) return std::numeric_limits<double>::max();
	else return x;
}

double Hyperbolic_Calculus::calcPriceFromPosition(double power, double asym, double neutral, double position) {
	return (power * neutral)/(position + power - power *  asym);
}

double Hyperbolic_Calculus::calcNeutralFromPrice0(double price0, double asym) {
	return (1 - asym) * price0;
}

double Hyperbolic_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice) {
	auto m = calcPrice0(neutral, asym);
	auto fncalc = [&](double x) {
		double neutral = calcNeutralFromPrice0(x, asym);
		double v = calcPosValue(power, asym, neutral, curPrice);
		return v - value;
	};

	if (fncalc(curPrice) > 0)
		return neutral;

	double res;
	if (curPrice > m) {
		res = numeric_search_r1(curPrice, fncalc);
	} else if (curPrice < m) {
		res = numeric_search_r2(curPrice, fncalc);
	} else {
		res = m;
	}
	return calcNeutralFromPrice0(res, asym);
}

double Linear_Calculus::calcPosValue(double power, double asym, double neutral, 	double curPrice) {
	return -(power * (neutral - curPrice) * ((-1 + 2 * asym) * neutral + curPrice))/(2 * neutral);
}

IStrategy::MinMax Linear_Calculus::calcRoots(double power, double asym, double neutral, double balance) {
	double a = sqrt(neutral*power*(asym*asym*neutral*power + 2 * balance))/power;
	double b = asym * neutral;
	double x1 = - a - b + neutral;
	double x2 = + a - b + neutral;
	return {x1,x2};
}

double Linear_Calculus::calcNeutral(double power, double asym, double position, double price) {
	return -(price * power)/(position + ( asym - 1) * power);
}

double Linear_Calculus::calcPosition(double power, double asym, double neutral, double price) {
	return -(price/neutral - 1 + asym) * power;
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
	value = -value;
	double r = (curPrice * power - asym * curPrice * power - value + sqrt(pow2(asym*curPrice*power) - 2 * curPrice* power * value + 2 * asym * curPrice * power * value + pow2(value)))/(power - 2 * asym * power);
	if (!finite(r)) return neutral;
	else return r;


}
