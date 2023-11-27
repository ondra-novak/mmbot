/*
 * sgn.h
 *
 *  Created on: 27. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_SGN_H_
#define SRC_MAIN_SGN_H_
#include <cmath>



template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

inline double pow2(double x) {
	return x*x;
}

inline bool similar(double a, double b, double e = 1e-8) {
    if (a == b) return true; // @suppress("Direct float comparison")

    double absA = std::abs(a);
    double absB = std::abs(b);
    double diff = std::abs(a - b);
    double sumAB = absA+absB;

    if ((a == 0) | (b == 0) | (sumAB < std::numeric_limits<double>::min())) { // @suppress("Direct float comparison")
        // a or b is zero or both are extremely close to it
        // relative error is less meaningful here
        return diff <=  e * std::numeric_limits<double>::min();
    }
    // use relative error
    return diff / std::min(sumAB, std::numeric_limits<double>::max()) < e;
}


#endif /* SRC_MAIN_SGN_H_ */
