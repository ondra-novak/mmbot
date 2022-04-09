/*
 * ssestream.h
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_SSESTREAM_H_
#define SRC_MAIN_SSESTREAM_H_

#include <shared/refcnt.h>
#include <userver/http_server.h>
#include "report.h"

class SSEStream: public ondra_shared::RefCntObj {
public:
	SSEStream(userver::PHttpServerRequest &&req);
	void init();
	bool on_event(const Report::StreamData &sdata);
protected:
	userver::PHttpServerRequest req;
	userver::Stream stream;
	bool closed;
	bool flushing;
	std::recursive_mutex mx;
	std::string buffer;
	ondra_shared::linear_map<std::size_t, std::size_t> hash_map;
	//called when flush is complete - result is true =ok, false = error
	void flushComplete(bool result);
	void flush();
	void monitor();

};




#endif /* SRC_MAIN_SSESTREAM_H_ */
