/*
 * abstractarray.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ABSTRACTARRAY_H_
#define SRC_MAIN_ABSTRACTARRAY_H_

#include <cstddef>

template<typename T>
class AbstractArray {
public:
	virtual T operator[](std::size_t idx) const = 0;
	virtual std::size_t size() const = 0;
	virtual ~AbstractArray() {}

	bool empty() const {return size() == 0;}
	T back() const {return this->operator [](size()-1);}
};





#endif /* SRC_MAIN_ABSTRACTARRAY_H_ */
