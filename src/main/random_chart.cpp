/*
 * random_chart.cpp
 *
 *  Created on: 3. 10. 2020
 *      Author: ondra
 */

#include "random_chart.h"

#include <random>

void generate_random_chart(double volatility, double noise, unsigned int minutes, std::uint64_t seed, std::vector<double> &prices) {
	std::mt19937 rgen(seed);

	double val = 1;
	double trend = 0;
	double cur_noise=0;
	unsigned int stop=60;
	std::uniform_int_distribution<> distr_n(0,1);
	for (unsigned int i = 0; i < minutes; i++) {
		if (i % stop == 0) {
			std::normal_distribution<> distr2;
			trend = distr2(rgen)*0.01*volatility;
			cur_noise = distr2(rgen)*noise*(1+std::abs(trend)*0.01);
			std::uniform_int_distribution<> distr3(60,1440);
			stop = distr3(rgen);
		}
		std::normal_distribution<> distr(trend);
		double diff = distr(rgen)*0.01*volatility;
		double ns = (1+(distr_n(rgen)*2-1)*cur_noise*0.01);
		val = val * (1+diff)*ns;
		prices.push_back(val);
	}





}

