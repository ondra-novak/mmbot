/*
 * numerical.h
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_NUMERICAL_H_
#define SRC_MAIN_NUMERICAL_H_


namespace {

const double accuracy = 1e-5;

}


template<typename Fn>
double numeric_search_r1(double middle, Fn &&fn) {
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
double numeric_search_r2(double middle, Fn &&fn) {
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



#endif /* SRC_MAIN_NUMERICAL_H_ */
