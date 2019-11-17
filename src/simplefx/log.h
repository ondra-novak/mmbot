/*
 * log.h
 *
 *  Created on: 16. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_CMAKEFILES_LOG_H_
#define SRC_SIMPLEFX_CMAKEFILES_LOG_H_
#include <imtjson/serializer.h>
#include <imtjson/value.h>


class LogJSON: public json::Value {
public:

	LogJSON(const json::Value &b):json::Value(b) {}
};

namespace ondra_shared {

template<typename WriteFn, typename A, typename B> void logPrintValue(WriteFn &wr, const std::pair<A,B> &v) {
	logPrintValue(wr,v.first);
	wr('=');
	logPrintValue(wr,v.second);
}

template<typename WriteFn> void logPrintValue(WriteFn &wr, LogJSON v) {
	v.serialize<WriteFn &>(wr);
}

}
template<typename Iter>
class LogRange {
public:
	LogRange(Iter &&iter1, Iter &&iter2, std::string_view sep):iter1(std::move(iter1)),iter2(std::move(iter2)), sep(sep) {}

	template<typename WriteFn>
	void logPrintValue(WriteFn &wr) const {
		wr('(');
		auto x = iter1;
		if (x != iter2) {
			ondra_shared::logPrintValue(wr, *x);
			++x;
			while (x != iter2) {
				wr(sep);
				ondra_shared::logPrintValue(wr, *x);
				++x;
			}
		}
		wr(')');
	}
protected:
	Iter iter1, iter2;
	std::string_view sep;


};

namespace ondra_shared {


template<typename WriteFn, typename Iter> void logPrintValue(WriteFn &wr, const LogRange<Iter> &v) {
	v.logPrintValue(wr);
}


}



#endif /* SRC_SIMPLEFX_CMAKEFILES_LOG_H_ */
