#include <imtjson/string.h>
#include <imtjson/object.h>

#include "quotestream.h"

#include <imtjson/namedEnum.h>
#include <chrono>

#include <shared/countdown.h>
#include <shared/logOutput.h>
#include "../../userver/ssl.h"
#include "../httpjson.h"
#include "../log.h"
#include "tradingengine.h"


using ondra_shared::logDebug;
using ondra_shared::logError;
using ondra_shared::logWarning;

QuoteStream::QuoteStream(std::string url, ReceiveQuotesFn &&cb)
:url(url)
,cb(std::move(cb))
,httpc({
	"Mozilla/5.0 (compatible; MMBot/3.0; +https://github.com/ondra-novak/mmbot)",
	10000,10000,
	nullptr,
	userver::sslConnectFn(),
	nullptr
})
{

}

QuoteStream::~QuoteStream() {
	stopped = true;
	Sync _(lock);
	if (ws != nullptr) {
		ws->close();
		_.unlock();
		thr.join();
		_.lock();
	}
}

static std::string urlEncode(std::string_view x) {
	std::string out;
	json::urlEncoding->encodeBinaryValue(json::map_str2bin(x),[&](std::string_view x){
		out.append(x);
	});
	return out;
}

inline std::shared_ptr<userver::WSStream> wsConnect(userver::HttpClient &httpclient, const userver::HttpClient::URL &url, std::string_view cookie, int *code_out = nullptr) {

	using namespace userver;
	if (code_out) *code_out = 0;
	std::unique_ptr<HttpClientRequest> req = httpclient.open("GET", url);
	if (req == nullptr) return nullptr;
	req->addHeader("Connection", "upgrade");
	req->addHeader("Upgrade","websocket");
	req->addHeader("Sec-WebSocket-Version","13");
	req->addHeader("Sec-WebSocket-Key","dGhlIHNhbXBsZSBub25jZQ==");
	req->addHeader("Cookie",cookie);
	int code = req->send();
	if (code_out) *code_out = code;
	if (code != 101) return nullptr;
	if (req->get("Upgrade") != "websocket") {
		if (code_out) *code_out = -1;
		return nullptr;
	}
	return std::make_shared<WSStream>(std::move(req->getStream()), true);
}


SubscribeFn QuoteStream::connect() {
	Sync _(lock);
	using namespace ondra_shared;

	HTTPJson hj(url);
	json::Value headers;
	json::Value v = hj.GET("negotiate?clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D&_="+std::to_string(TradingEngine::now()),std::move(headers));
	json::Value cookie = headers["set-cookie"];
	std::string cookeText = StrViewA(StrViewA(cookie.getString()).split(";")()).trim(isspace);
	json::Value reqhdrs = json::Object({{"cookie", cookeText}});



	std::string enctoken = urlEncode(v["ConnectionToken"].toString());

	std::string wsurl = url+"connect?transport=webSockets&ConnectionToken="+enctoken+"&clientProtocol=1.5&connectionData=%5B%7B%22name%22%3A%22quotessubscribehub%22%7D%5D";
	logDebug("Opening stream: $1", wsurl);

	int code;
	ws = wsConnect(hj.getClient(),wsurl, cookeText, &code);
	if (ws == nullptr) throw std::runtime_error(std::string("Failed to connect: error ")+std::to_string(code));


	ondra_shared::Countdown cnt(1);

	std::thread t2([this, &cnt]{
		auto  t = ws->recvSync();
		while (t != userver::WSFrameType::text) {
			logDebug("Received frame type: $1", (int)t);
			if (t == userver::WSFrameType::incomplete || t == userver::WSFrameType::connClose) {
				break;
			}
			t = ws->recvSync();

		}
		logDebug("Received initial frame : $1 - connected", ws->getData());
		cnt.dec();
		try {
			if (t != userver::WSFrameType::incomplete) {
				processMessages();
			} else {
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
		auto body = e.body;
		logError("Unable to start stream: $1", body.toString().str());
		throw;
	}

	auto subscribeFn = [this](const std::string_view &symbol) {
		Sync _(lock);

		json::Value A = json::Value(json::array,{json::Value(json::array,{symbol})});
		json::Value data = json::Object({
			{"H","quotessubscribehub"},
			{"M","getLastPrices"},
			{"A",A},
			{"I",this->cnt++}});
		ws->send(userver::WSFrameType::text, data.stringify().str());
		data = json::Object({
			{"H","quotessubscribehub"},
			{"M","subscribeList"},
			{"A",A},
			{"I",this->cnt++}});
		ws->send(userver::WSFrameType::text, data.stringify().str());

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

			json::Value data = json::Object({
					{"H", "quotessubscribehub"},
					{"M","unsubscribeList"},
					{"A", json::Value(json::array, {json::Value(json::array, { s }) })},
					{"I", this->cnt++}});
			ws->send(userver::WSFrameType::text, data.stringify().str());
			subscribed.erase(s.getString());
			logDebug("--- Unsubscribed $1, currently: $2", s, LogRange<decltype(subscribed.begin())>(subscribed.begin(), subscribed.end(), ","));
		}
	}
}

json::NamedEnum<userver::WSFrameType> frameTypes({
	{userver::WSFrameType::binary,"binary"},
	{userver::WSFrameType::connClose,"connClose"},
	{userver::WSFrameType::incomplete,"incomplete (EOF)"},
	{userver::WSFrameType::init,"init"},
	{userver::WSFrameType::ping,"ping"},
	{userver::WSFrameType::pong,"pong"},
	{userver::WSFrameType::text,"text"},
});

void QuoteStream::processMessages() {
	do {
		if (ws->getFrameType() == userver::WSFrameType::text) {
			try {
				json::Value data = json::Value::fromString(ws->getData());

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
				logError("Exception: $1 (discarded frame: $2)", e.what(), ws->getData());
			}
		}
	} while (ws->recvSync() != userver::WSFrameType::incomplete);

	logWarning("Stream closed - reconnect (frameType: $1)", frameTypes[ws->getFrameType()]);

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
