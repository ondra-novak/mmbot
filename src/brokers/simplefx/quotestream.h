/*
 * simplesignalr.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_QUOTESTREAM_H_
#define SRC_SIMPLEFX_QUOTESTREAM_H_

#include <thread>
#include <shared/linear_set.h>
#include "../../userver/http_client.h"
#include "../../userver/websockets_stream.h"


#include "datasrc.h"

using userver::WSStream;


class QuoteStream {
public:

	QuoteStream(std::string url, ReceiveQuotesFn &&cb);
	~QuoteStream();

	SubscribeFn connect();

protected:



	std::string url;
	ReceiveQuotesFn cb;
	bool stopped = false;

	userver::HttpClient httpc;
	std::shared_ptr<userver::WSStream> ws;

	std::thread thr;
	std::recursive_mutex lock;
	using Sync = std::unique_lock<std::recursive_mutex>;
	unsigned int cnt;

	void processMessages();
	void reconnect();
	void processQuotes(const json::Value& quotes);
	ondra_shared::linear_set<std::string> subscribed;
};




#endif /* SRC_SIMPLEFX_QUOTESTREAM_H_ */
