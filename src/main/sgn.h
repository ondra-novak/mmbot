/*
 * sgn.h
 *
 *  Created on: 27. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_SGN_H_
#define SRC_MAIN_SGN_H_



template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

inline double pow2(double x) {
	return x*x;
}

inline bool similar(double a, double b, double e) {
	double c1 = std::fabs(a-b);
	double c2 = (std::fabs(a)+std::fabs(b))/2;

	return c1 == 0 || (c1/c2) <= e;

}

#endif /* SRC_MAIN_SGN_H_ */
