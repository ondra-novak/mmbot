/*
 * series.cpp
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#include "series.h"

#include <cmath>

StreamSUM::StreamSUM(std::size_t interval):interval(interval),sum() {
}

double StreamSUM::operator <<(double v) {
	sum+=v;
	n.push(v);
	if (n.size()>interval) {
		sum-=n.front();
		n.pop();
	}
	return sum;
}

std::size_t StreamSUM::size() const {
	return n.size();
}

StreamSMA::StreamSMA(std::size_t interval):sum(interval) {

}

double StreamSMA::operator <<(double v) {
	double s = sum << v;
	return s/sum.size();
}

std::size_t StreamSMA::size() const {
	return sum.size();
}

StreamSTDEV::StreamSTDEV(std::size_t interval):sum(interval) {
}

double StreamSTDEV::operator <<(double v) {
	double s = sum << (v*v);
	return std::sqrt(s/sum.size());
}

std::size_t StreamSTDEV::size() const {
	return sum.size();
}
