/*
 * random_chart.h
 *
 *  Created on: 3. 10. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_RANDOM_CHART_H_
#define SRC_MAIN_RANDOM_CHART_H_
#include <vector>


void generate_random_chart(double volatility, double noise, unsigned int minutes, std::size_t seed, std::vector<double> &prices);



#endif /* SRC_MAIN_RANDOM_CHART_H_ */
