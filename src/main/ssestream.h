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
#include <userver/callback.h>

#include "report.h"

class SSEStream: public ondra_shared::RefCntObj {
public:
	SSEStream(userver::PHttpServerRequest &&req);
	~SSEStream();
	bool init(bool monitor_close);
	bool on_event(const Report::StreamData &sdata);
	void close();

	///Tests size of buffer and if buffer is above certain size, slows operation
	/**
	 * @param sz test size,
	 * @param cb_construct function called to construct callback function. It is not callback
	 * function itself, it must prepare callback function and returns it as result. The
	 * main motivation is to avoid costly preparations when buffer is not full enough. The function
	 * is called synchronously but under lock, so it can have a closure containing references
	 * to the main scope.
	 * @retval true waiting
	 * @retval false no wait is needed
	 */
	template<typename Fn>
	bool wait_if_buffer_full(std::size_t sz, Fn &&cb_construct);
protected:
	userver::PHttpServerRequest req;
	userver::Stream stream;
	bool needsse;
	bool closed;
	bool flushing;
	std::recursive_mutex mx;
	std::string buffer;
	ondra_shared::linear_map<std::size_t, std::size_t> hash_map;
	std::vector<userver::Callback<void()> > flush_cb;
	//called when flush is complete - result is true =ok, false = error
	void flushComplete(bool result);
	void flush();
	void monitor();

	std::string_view prefix, suffix;

};

using PSSEStream = ondra_shared::RefCntPtr<SSEStream>;

template<typename Fn>
bool SSEStream::wait_if_buffer_full(std::size_t sz, Fn &&cb_construct) {
	std::lock_guard _(mx);
	if (buffer.size()<=sz) return false;
	flush_cb.push_back(cb_construct());
	return true;
}

#endif /* SRC_MAIN_SSESTREAM_H_ */
