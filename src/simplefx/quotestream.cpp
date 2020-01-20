#include <simpleServer/urlencode.h>
#include <imtjson/string.h>
#include <imtjson/object.h>

#include "quotestream.h"

#include <imtjson/namedEnum.h>
#include <chrono>

#include "../shared/countdown.h"
#include "../shared/logOutput.h"
#include "../brokers/httpjson.h"
#include "../brokers/log.h"
#include "tradingengine.h"


using ondra_shared::logDebug;
using ondra_shared::logError;
using ondra_shared::logWarning;

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

	HTTPJson hj(simpleServer::HttpClient(httpc),url);
	json::Value headers;
	json::Value v = hj.GET("negotiate?clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D&_="+std::to_string(TradingEngine::now()),std::move(headers));
	json::Value cookie = headers["set-cookie"];
	std::string cookeText = StrViewA(cookie.getString().split(";")()).trim(isspace);
	json::Value reqhdrs = json::Object("cookie", cookeText);



	std::string enctoken = simpleServer::urlEncode(v["ConnectionToken"].toString());

	std::string wsurl = url+"connect?transport=webSockets&ConnectionToken="+enctoken+"&clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D";
	logDebug("Opening stream: $1", wsurl);

	ws = simpleServer::connectWebSocket(httpc, wsurl, simpleServer::SendHeaders()("cookie", cookeText));
	ws.getStream().setIOTimeout(30000);

	ondra_shared::Countdown cnt(1);

	std::thread t2([this, &cnt]{
		bool rr = ws.readFrame();
		while (rr && ws.getFrameType() != simpleServer::WSFrameType::text) {
			logDebug("Received frame type: $1", (int)ws.getFrameType());
			rr = ws.readFrame();
		}
		logDebug("Received initial frame : $1 - connected", ws.getText());
		cnt.dec();
		try {
			if (rr) processMessages(); else {
				std::this_thread::sleep_for(std::chrono::seconds(10));
				reconnect();
			}
		} catch (std::exception &e) {
			logError("Stream error: $1 - reconnect", e.what());
			reconnect();
		}
	});
	cnt.wait();
	thr = std::move(t2);

	std::string starturl = "start?transport=webSockets&ConnectionToken="+enctoken+"&clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D&_="+std::to_string(TradingEngine::now());
	try {
		hj.GET(starturl,std::move(reqhdrs));
	} catch (const HTTPJson::UnknownStatusException &e) {
		std::string s;
		auto body = e.response.getBody();
		StrViewA c = StrViewA(body.read());
		while (!c.empty()) {
			s.append(c.data, c.length);
			c = StrViewA(body.read());
		}
		logError("Unable to start stream: $1", s);
		throw;
	}

	auto subscribeFn = [this](const std::string_view &symbol) {
		Sync _(lock);

		json::Value A = json::Value(json::array,{json::Value(json::array,{symbol})});
		json::Value data = json::Object
				("H","quotessubscribehub")
				("M","getLastPrices")
				("A",A)
				("I",this->cnt++);
		ws.postText(data.stringify());
		data = json::Object
				("H","quotessubscribehub")
				("M","subscribeList")
				("A",A)
				("I",this->cnt++);
		ws.postText(data.stringify());

		subscribed.insert(std::string(symbol));
		logDebug("+++ Subscribed $1, currently: $2", symbol, LogRange<decltype(subscribed.begin())>(subscribed.begin(), subscribed.end(), ","));
	};

	auto oldlst = std::move(subscribed);
	for (auto &&x: oldlst) subscribeFn(x);

	return subscribeFn;
}

void QuoteStream::processQuotes(const json::Value& quotes) {
	for (json::Value q : quotes) {
		json::Value s = q["s"];
		json::Value a = q["a"];
		json::Value b = q["b"];
		json::Value t = q["t"];
		if (!cb(s.getString(), b.getNumber(), a.getNumber(), t.getUIntLong()*1000)) {
			Sync _(lock);

			json::Value data = json::Object("H", "quotessubscribehub")("M",
					"unsubscribeList")("A", json::Value(json::array, {
					json::Value(json::array, { s }) }))("I", this->cnt++);
			ws.postText(data.stringify());
			subscribed.erase(s.getString());
			logDebug("--- Unsubscribed $1, currently: $2", s, LogRange<decltype(subscribed.begin())>(subscribed.begin(), subscribed.end(), ","));
		}
	}
}

json::NamedEnum<simpleServer::WSFrameType> frameTypes({
	{simpleServer::WSFrameType::binary,"binary"},
	{simpleServer::WSFrameType::connClose,"connClose"},
	{simpleServer::WSFrameType::incomplete,"incomplete (EOF)"},
	{simpleServer::WSFrameType::init,"init"},
	{simpleServer::WSFrameType::ping,"ping"},
	{simpleServer::WSFrameType::pong,"pong"},
	{simpleServer::WSFrameType::text,"text"},
});

void QuoteStream::processMessages() {
	do {
		if (ws.getFrameType() == simpleServer::WSFrameType::text) {
			try {
				json::Value data = json::Value::fromString(ws.getText());

				json::Value R = data["R"];
				if (R.defined()) {
					json::Value quotes = R["data"];
					processQuotes(quotes);
				} else {
					json::Value C = data["C"];
					if (C.defined()) {
						json::Value M = data["M"];
						for (json::Value x: M) {
							json::Value H = x["H"];
							json::Value M = x["M"];
							json::Value A = x["A"];
							if (H.getString() == "QuotesSubscribeHub" && M == "ReceiveQuotes") {
								json::Value quotes = A[0];
								processQuotes(quotes);
							}
						}
					}
				}
			} catch (std::exception &e) {
				logError("Exception: $1 (discarded frame: $2)", e.what(), ws.getText());
			}
		}
	} while (ws.readFrame());

	logWarning("Stream closed - reconnect (frameType: $1)", frameTypes[ws.getFrameType()]);

	std::this_thread::sleep_for(std::chrono::seconds(3));

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
			} catch (std::exception &e) {
				logError("QuoteStream reconnect error: $1", e.what());
				std::this_thread::sleep_for(std::chrono::seconds(3));
			}
		}
	}
}
