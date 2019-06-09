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

#endif /* SRC_MAIN_SGN_H_ */
