/*
 * strategy_elliptical.cpp
 *
 *  Created on: 7. 6. 2020
 *      Author: ondra
 */


#include <cmath>
#include "strategy_elliptical.h"
#include "strategy_leveraged_base.tcc"
#include "sgn.h"

std::string_view Elliptical_Calculus::id = "elliptical";

Elliptical_Calculus::Elliptical_Calculus(double width, bool inverted):_width(width),_inverted(inverted) {
}

double Elliptical_Calculus::calcPosValue(double power, double asym, double neutral, double curPrice)  const{
	double width = this->width(neutral);
	double sq= pow2(width)-pow2(curPrice - neutral);
	if (sq < 0) sq = 0;
	return -power * ((std::sqrt(sq) + asym*(curPrice - neutral))/width - 1);
}

double Elliptical_Calculus::calcPosition(double power, double asym, double neutral, double price)  const{
	double width = this->width(neutral);
	double sq = pow2(pow2(width)) - pow2(width)*pow2(price - neutral);
	if (sq < 0) return 0;
	return -power * ((price-neutral)/std::sqrt(sq) - asym/width);
}

double Elliptical_Calculus::calcPower(double neutral, double balance, double asym)  const{
	return balance/(1+std::abs(asym));
}

static std::pair<double,double> calcPosCore(double width, double power, double asym, double price, double position) {
	// -p*((x-k)/sqrt(...) - a/w) = t;
	//   (x-k)/sqrt(...) - a/w = -t/p
	//   (x-k)/sqrt(...)  = a/w - t/p
	// s= a/w - t/p
	double s = asym/width - position/power, s2 = s*s, s4 = s2*s2;
	double k = price;
	double w = width,w2 = w*w, w4=w2*w2, w6=w4*w2;

	double sq = std::sqrt(s4*w6 + s2*w4);
	double dnm = s2 * w2 + 1;
	double cnm = dnm * k;

	double x1 = (cnm + sq)/dnm;
	double x2 = (cnm - sq)/dnm;

	return {x1,x2};

}


double Elliptical_Calculus::calcNeutral(double power, double asym, double position, double curPrice)  const{
	double ref = curPrice;
	double n = curPrice;
	double w = 0;
	int cnt = 0;
	do {
		ref = n;
		double width = this->width(n);
		w = (w * cnt + width)/(cnt+1);
		auto x12 = calcPosCore(w, power, asym, curPrice, position);
		double x1 = x12.first ;
		double x2 = x12.second  ;

		double p1 = calcPosition(power, asym, x1, curPrice);
		double p2 = calcPosition(power, asym, x2, curPrice);
		n = (x1 > 0 && std::abs(p1-position)<std::abs(p2-position))?x1:x2;
		cnt++;
	} while (pow2((ref - n)/n) > 1e-10 && cnt < 32);
	return n;
}

///https://www.desmos.com/calculator/js57nrj2ou

double Elliptical_Calculus::calcPrice0(double neutral_price, double asym)  const{
	return calcPriceFromPosition(1,asym,neutral_price,0);
}


double Elliptical_Calculus::calcPriceFromPosition(double power,	double asym, double neutral, double position)  const{
	double width = this->width(neutral);
	auto x12 = calcPosCore(width, power, asym, neutral, position);

	double p1 = calcPosition(power, asym, neutral, x12.first);
	double p2 = calcPosition(power, asym, neutral, x12.second);
	return (std::abs(p1-position)<std::abs(p2-position))?x12.first:x12.second;
}


static std::pair<double,double> calcValCore(double w, double a, double p, double v) {
	double s = 1 - v/p;
	double sq = std::sqrt(a*a*w*w - s*s*w*w + w*w);
	if (!std::isfinite(sq)) return {sq,sq};
	double asw = a*s*w;
	double a21 = a*a+1;
	double x1 = (asw - sq)/a21;
	double x2 = (asw + sq)/a21;
	return {x1,x2};
}


double Elliptical_Calculus::calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice)  const{
		double width = this->width(neutral);
		auto x12 = calcValCore(width, asym, power, value);
		if (!std::isfinite(x12.first)) return neutral;
		double x1 = curPrice - x12.first;
		double x2 = curPrice - x12.second;
		return (std::abs(x1-neutral)<std::abs(x2-neutral))?x1:x2;
}

IStrategy::MinMax Elliptical_Calculus::calcRoots(double power, double asym, double neutral, double balance)  const{
	double width = this->width(neutral);
//	double cv = calcPosValue(power,asym,neutral,calcPrice0(neutral,asym));
	auto x12 = calcValCore(width, asym, power, balance);
	if (!std::isfinite(x12.first)) return {neutral - width, neutral + width};
	double x1 = std::min(x12.first,x12.second) + neutral;
	double x2 = std::max(x12.first,x12.second) + neutral;
	double minbal = power*(1+asym);
	double maxbal = power*(1-asym);
	if (balance > minbal) x1 = neutral - width;
	if (balance > maxbal) x2 = neutral + width;
	return {x1,x2};

}

bool Elliptical_Calculus::isValid(const IStockApi::MarketInfo &minfo) const {
	return _inverted == minfo.invert_price;
}

Elliptical_Calculus Elliptical_Calculus::init(const IStockApi::MarketInfo &minfo) const {
	return Elliptical_Calculus(_width, minfo.invert_price);
}

double Elliptical_Calculus::width(double neutral) const {
	if (_width == 0) return neutral*0.5;
	if (_inverted) {
		double price = 1/neutral;
		double low_range = price + _width;
		return neutral - 1/low_range ;
	} else {
		return _width;
	}
}


template class Strategy_Leveraged<Elliptical_Calculus> ;

