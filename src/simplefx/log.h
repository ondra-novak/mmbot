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


template<typename WriteFn> void logPrintValue(WriteFn &wr, LogJSON v) {
	v.serialize<WriteFn &>(wr);
}

}



#endif /* SRC_SIMPLEFX_CMAKEFILES_LOG_H_ */
