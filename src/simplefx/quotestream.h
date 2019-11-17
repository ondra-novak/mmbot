/*
 * simplesignalr.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_QUOTESTREAM_H_
#define SRC_SIMPLEFX_QUOTESTREAM_H_

#include <thread>
#include <simpleServer/websockets_stream.h>
#include <simpleServer/http_client.h>
#include "../shared/linear_set.h"


#include "datasrc.h"


class QuoteStream {
public:

	QuoteStream(simpleServer::HttpClient &httpc, std::string url, ReceiveQuotesFn &&cb);
	~QuoteStream();

	SubscribeFn connect();

protected:



	std::string url;
	ReceiveQuotesFn cb;
	bool stopped = false;

	simpleServer::HttpClient &httpc;
	simpleServer::WebSocketStream ws;

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
