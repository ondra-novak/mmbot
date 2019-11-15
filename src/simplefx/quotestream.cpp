#include <simpleServer/urlencode.h>
#include <imtjson/string.h>
#include <imtjson/object.h>

#include "quotestream.h"

#include <chrono>

#include "../shared/countdown.h"
#include "httpjson.h"

QuoteStream::QuoteStream(simpleServer::HttpClient &httpc, std::string url, ReceiveQuotesFn &&cb)
:url(url)
,cb(std::move(cb))
,httpc(httpc)
{

}

QuoteStream::~QuoteStream() {
	stopped = true;
	Sync _(lock);
	if (ws != nullptr) {
		ws.close();
		_.unlock();
		thr.join();
		_.lock();
	}
}

SubscribeFn QuoteStream::connect() {
	Sync _(lock);

	HTTPJson hj(httpc,url);
	json::Value v = hj.GET("negotiate?clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D");

	std::string enctoken = simpleServer::urlEncode(v["ConnectionToken"].toString());

	std::string wsurl = "connect?transport=webSockets&ConnectionToken="+enctoken+"&clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D";

	ws = simpleServer::connectWebSocket(httpc, wsurl, simpleServer::SendHeaders());

	ondra_shared::Countdown cnt(1);

	std::thread t2([this, &cnt]{
		bool rr = ws.readFrame();
		cnt.dec();
		try {
			if (rr) processMessages(); else {
				std::this_thread::sleep_for(std::chrono::seconds(10));
				reconnect();
			}
		} catch (...) {
			reconnect();
		}
	});

	thr = std::move(t2);

	std::string starturl = "start?transport=webSockets&ConnectionToken="+enctoken+"&clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D";
	v = hj.GET(starturl);

	return [this](const std::string_view &symbol) {
		Sync _(lock);

		json::Value data = json::Object
				("H","quotessubscribehub")
				("M","subscribeList")
				("A",json::Value(json::array,{json::Value(json::array,{symbol})}))
				("I",this->cnt++);
		ws.postText(data.stringify());
	};
}

void QuoteStream::processMessages() {
	do {
		if (ws.getFrameType() == simpleServer::WSFrameType::text) {
			try {
				json::Value data = json::Value::fromString(ws.getText());

				json::Value C = data["C"];
				if (C.defined()) {
					json::Value M = data["M"];
					for (json::Value x: M) {
						json::Value H = x["H"];
						json::Value M = x["M"];
						json::Value A = x["A"];
						if (H.getString() == "QuotesSubscribeHub" && M == "ReceiveQuotes") {
							json::Value quotes = A[0];
							for (json::Value q: quotes) {
								json::Value s = q["s"];
								json::Value a = q["a"];
								json::Value b = q["b"];
								json::Value t = q["t"];
								cb(s.getString(), b.getNumber(), a.getNumber(), t.getUIntLong());
							}
						}
					}
				}
			} catch (...) {
				break;
			}
		}
	} while (ws.readFrame());

	reconnect();
}

void QuoteStream::reconnect() {
	Sync _(lock);

	if (!this->stopped) {
		thr.detach();
		while (!this->stopped) {
			try {
				connect();
				break;
			} catch (...) {
				//TODO: logObject
				std::this_thread::sleep_for(std::chrono::seconds(3));
			}
		}
	}
}
