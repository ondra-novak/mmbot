#include <simpleServer/urlencode.h>
#include <imtjson/string.h>
#include <imtjson/object.h>

#include "quotestream.h"

#include <imtjson/namedEnum.h>
#include <chrono>

#include <shared/countdown.h>
#include <shared/logOutput.h>
#include "../httpjson.h"
#include "../log.h"
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

	ws = simpleServer::connectWebSocket(httpc, url);
    ws.getStream().setIOTimeout(30000);
    thr = std::thread([this]{
        processMessages();
    });
    if (!subscribed.empty()) {
        json::Value data = json::Object({
            {"p","/subscribe/addList"},
            {"i", cnt++},
            {"d", json::Value(json::array, subscribed.begin(), subscribed.end())}
        });
        auto str = data.stringify();
        logDebug("WebSocket: $1", str.str());
        ws.postText(str.str());
    }

    return [this](const std::string_view &symbol) {
        Sync _(lock);
        if (subscribed.insert(std::string(symbol)).second) {
            json::Value data = json::Object({
                {"p","/subscribe/addList"},
                {"i", cnt++},
                {"d", {symbol}}
            });
            auto str = data.stringify();
            logDebug("WebSocket: $1", str.str());
            ws.postText(str.str());
        }
    };

}

void QuoteStream::processQuotes(const json::Value& quotes, bool first_frame) {
	for (json::Value q : quotes) {
		json::Value s = q["s"];
		json::Value a = q["a"];
		json::Value b = q["b"];
		json::Value t = q["t"];
		if (!cb(s.getString(), b.getNumber(), a.getNumber(), t.getUIntLong()*1000) && !first_frame) {
			Sync _(lock);

			json::Value data = json::Object({
					{"p", "/subscribe/removeList"},
					{"i",cnt++},
					{"d", json::Value(json::array,{s})}
			    });
	        auto str = data.stringify();
	        logDebug("WebSocket: $1", str.str());
			ws.postText(str.str());
			subscribed.erase(s.getString());
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
    bool no_data = false;
    bool first_frame = true;
	while (ws.readFrame()) {
		if (ws.getFrameType() == simpleServer::WSFrameType::text) {
		    logDebug("WebSocket income: $1", ws.getText());
		    no_data = false;
			try {
				json::Value data = json::Value::fromString(ws.getText());
				if (data["p"] == "/quotes/subscribed") {
				    processQuotes(data["d"], first_frame);
				    first_frame = false;
				}
			} catch (std::exception &e) {
				logError("Exception: $1 (discarded frame: $2)", e.what(), ws.getText());
			}
		} else if (ws.getFrameType() == simpleServer::WSFrameType::incomplete) {
		    if (no_data) {
		        logDebug("WebSocket frozen");
		        break;
		    }
            logDebug("WebSocket ping");
		    no_data = true;
		    ws->ping({});
		} else {
		    no_data = false;
		}
	}

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
