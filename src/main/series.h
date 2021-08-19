/*
 * series.h
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SERIES_H_
#define SRC_MAIN_SERIES_H_
#include <cstddef>
#include <vector>
#include <optional>
#include <queue>



class StreamSUM {
public:

	StreamSUM(std::size_t interval);
	//feed value and return result
	double operator<<(double v);
	std::size_t size() const;
protected:
	std::size_t interval;
	std::queue<double> n;
	double sum;

};

class StreamSMA {
public:

	StreamSMA(std::size_t interval);
	//feed value and return result
	double operator<<(double v);
	std::size_t size() const;
protected:
	StreamSUM sum;
};


class StreamSTDEV {
public:
	StreamSTDEV(std::size_t interval);
	double operator<<(double v);
	std::size_t size() const;
protected:
	StreamSUM sum;

};


#endif /* SRC_MAIN_SERIES_H_ */
