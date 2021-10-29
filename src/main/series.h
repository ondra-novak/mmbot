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

template<typename T, typename Cmp>
class StreamBest {
public:
	StreamBest(std::size_t interval, Cmp cmp = Cmp()):cmp(cmp),interval(interval) {}
	T operator<<(const T &val);
	std::size_t size() const;
protected:
	Cmp cmp;
	std::size_t interval;
	std::deque<T> data;
	std::optional<T> best;
};

template<typename T, typename Cmp>
inline T StreamBest<T, Cmp>::operator <<(const T &val) {
	if (!best.has_value()) best = val;
	else if (cmp(val, *best)) {
		best = val;
		data.push_back(val);
		if (data.size()>interval) data.pop_front();
	} else {
		data.push_back(val);
		if (data.size() > interval) {
			const T &l = data.front();
			bool findmax = l == *best;
			data.pop_front();
			if (findmax) {
				T curBest = val;
				for (const T &x: data) {
					if (cmp(x, curBest)) curBest = x;
				}
				best = curBest;
			}
		}
	}
	return *best;
}

template<typename T, typename Cmp>
inline std::size_t StreamBest<T, Cmp>::size() const {
	return data.size();
}

#endif /* SRC_MAIN_SERIES_H_ */
