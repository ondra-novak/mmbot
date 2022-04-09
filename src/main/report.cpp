/*
 * report.cpp
 *
 *  Created on: 17. 5. 2019
 *      Author: ondra
 */

#include "report.h"

#include <imtjson/value.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
#include <chrono>
#include <numeric>
#include <imtjson/binary.h>
#include <imtjson/string.h>

#include <shared/linear_map.h>
#include <shared/logOutput.h>
#include <shared/range.h>
#include <shared/stdLogOutput.h>
#include "sgn.h"
#include "../../version.h"
#include "ibrokercontrol.h"

#include "alert.h"

#include "acb.h"

using ondra_shared::logError;
using namespace std::chrono;

using namespace json;

void Report::setInterval(std::uint64_t interval) {
	this->interval_in_ms = interval;
}


json::Value Report::genReport_noStore() {
	Object st;
	exportCharts(st.object("charts"));
	exportOrders(st.array("orders"));
	exportTitles(st.object("info"));
	exportPrices(st.object("prices"));
	exportMisc(st.object("misc"));
	st.set("interval", interval_in_ms);
	st.set("rev", counter);
	st.set("log", logLines);
	st.set("performance", perfRep);
	st.set("version", MMBOT_VERSION);
	st.set("news", newsMessages);
	return st;
}

Report::StreamData Report::ev_clear_cache = {true,nullptr, 0, 0};
Report::StreamData Report::ev_refresh = {true,"refresh", 0, 0};
Report::StreamData Report::ev_end_refresh = {true,"end_refresh", 0, 0};
Report::StreamData Report::ev_update = {true,"update",0,0};
Report::StreamData Report::ev_ping = {true,"ping",0,0};



void Report::genReport() {
	while (logLines.size()>30) logLines.erase(logLines.begin());
	counter++;
	report->store(genReport_noStore());

	if (refresh_after_clear) {
		refresh_after_clear = false;

		for (auto &x : streams) {
			x(ev_clear_cache); //< clear cache.
			x(ev_refresh) && stream_refresh(x) && x(ev_end_refresh);
		}
	} else {
		for (auto &x : streams) {
			x(ev_update);
		}
		sendStreamGlobal(*this);
	}
}



struct ChartData {
	Array records;
	double last_price = 0;
	double sums = 0;
	double assets = 0;
	double init_price = 0;
};

template<typename ME>
void Report::sendStreamOrder(ME &me, const OKey &key, const OValue &data) {
	me.sendStream(
			json::Object{
		{ "type", "order" },
		{ "symbol", key.symb },
		{ "dir", key.dir }},
		data.toJson()
	);
}

void Report::setOrders(std::size_t rev, std::string_view symb, int n, const std::optional<IStockApi::Order> &buy,
	  	  	  	  	  	  	  	     const std::optional<IStockApi::Order> &sell) {
	if (rev != revize) return;
	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();

	int buyid = inverted?-n:n;

	OKey buyKey {std::string(symb), buyid};
	OKey sellKey {std::string(symb), -buyid};
	OValue data;

	if (buy.has_value()) {
		data = {inverted?1.0/buy->price:buy->price, buy->size*buyid};
	} else{
		data = {0, 0};
	}

	orderMap[buyKey] = data;
	sendStreamOrder(*this,buyKey, data);


	if (sell.has_value()) {
		data = {inverted?1.0/sell->price:sell->price, sell->size*buyid};
	} else {
		data = {0, 0};
	}

	orderMap[sellKey] = data;
	sendStreamOrder(*this,sellKey, data);


}

static double wavg(double a, double wa, double b, double wb) {
	double s = wa + wb;
	if (s == 0) return 0;
	return (a * wa + b * wb)/s;
}

static IStatSvc::TradeRecord sumTrades(const IStatSvc::TradeRecord &a, const IStatSvc::TradeRecord &b) {
	return IStatSvc::TradeRecord(
			IStockApi::Trade {
				b.id,b.time,
				a.size+b.size,
				wavg(a.price,a.size,b.price,b.size),
				a.eff_size+b.eff_size,
				wavg(a.eff_price,a.eff_size,b.eff_price,b.eff_size),
			},
			b.norm_profit,
			b.norm_accum,
			b.neutral_price,b.manual_trade
	);
}

template<typename ME>
void Report::sendStreamTrades(ME &me, const std::string_view &symb, const json::Value &records) {
	for (Value rw : records) {
		me.sendStream(
			json::Object{
			{ "type", "trade" },
			{ "symbol",symb},
			{ "id", rw["iid"]}},
			rw.replace("iid",json::undefined)
		);
	}
}

static json::NamedEnum<AlertReason> strAlertReason({
	{AlertReason::unknown, "unknown"},
	{AlertReason::below_minsize, "below_minsize"},
	{AlertReason::accept_loss, "accept_loss"},
	{AlertReason::max_cost, "max_cost"},
	{AlertReason::no_funds, "no_funds"},
	{AlertReason::max_leverage, "max_leverage"},
	{AlertReason::out_of_budget, "out_of_budget"},
	{AlertReason::position_limit, "position_limit"},
	{AlertReason::strategy_enforced, "strategy_enforced"},
	{AlertReason::strategy_outofsync, "strategy_outofsync"},
	{AlertReason::initial_reset, "initial_reset"}
});

void Report::setTrades(std::size_t rev, std::string_view symb, double finalPos, StringView<IStatSvc::TradeRecord> trades) {

	if (rev != revize) return;

	using ondra_shared::range;

	json::Array records;

	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();
	double chng = std::accumulate(trades.begin(), trades.end(), 0.0, [](double x, const IStatSvc::TradeRecord &b){
		return x+b.eff_size;
	});
	double pos = finalPos-chng;

	if (!trades.empty()) {

		const auto &last = trades[trades.length-1];
		std::uint64_t last_time = last.time;
		std::uint64_t first = last_time - interval_in_ms;


		auto tend = trades.end();
		auto iter = trades.begin();
		auto &&t = *iter;


		double init_price = t.eff_price;


		double prev_price = init_price;
		double cur_fromPos = 0;
		double pnp = 0;
		double pap = 0;

		ACB acb(init_price, pos);
		bool normaccum = false;

		std::optional<IStatSvc::TradeRecord> tmpTrade;
		const IStatSvc::TradeRecord *prevTrade = nullptr;


		int iid = 0;
		do {
			if (iter == tend || (prevTrade && (std::abs(prevTrade->price - iter->price) > std::abs(iter->price*1e-8)
												|| prevTrade->size * iter->size <= 0
												|| prevTrade->manual_trade
												|| iter->manual_trade)))
				{

				auto &&t = *prevTrade;

				double gain = (t.eff_price - prev_price)*pos ;

				prev_price = t.eff_price;

				acb = acb(t.eff_price, t.eff_size);

				cur_fromPos += gain;
				pos += t.eff_size;



				double normch = (t.norm_accum - pap) * t.eff_price + (t.norm_profit - pnp);
				pap = t.norm_accum;
				pnp = t.norm_profit;
				normaccum = normaccum || t.norm_accum != 0;



				if (t.time >= first) {
					records.push_back(Object({
						{"id", t.id},
						{"time", t.time},
						{"achg", (inverted?-1:1)*t.size},
						{"gain", gain},
						{"norm", t.norm_profit},
						{"normch", normch},
						{"nacum", normaccum?Value((inverted?-1:1)*t.norm_accum):Value()},
						{"pos", (inverted?-1:1)*pos},
						{"pl", cur_fromPos},
						{"rpl", acb.getRPnL()},
						{"open", acb.getOpen()},
						{"iid", std::to_string(iid)},
						{"price", (inverted?1.0/t.price:t.price)},
						{"p0",t.neutral_price?Value(inverted?1.0/t.neutral_price:t.neutral_price):Value()},
						{"volume", fabs(t.eff_price*t.eff_size)},
						{"man",t.manual_trade},
						{"alert", t.size == 0?Value(Object{
							{"reason",strAlertReason[static_cast<AlertReason>(t.alertReason)]},
							{"side", t.alertSide}
						}):json::Value()}
					}));
				}
				prevTrade = nullptr;
				if (iter == tend)
					break;
			}
			if (prevTrade == nullptr) {
				prevTrade = &(*iter);
			} else {
				tmpTrade = sumTrades(*prevTrade, *iter);
				prevTrade = &(*tmpTrade);
				--iid;
			}

			++iter;
			iid++;
		} while (true);

	}
	tradeMap[symb] = records;
	sendStreamTrades(*this,symb, records);
}


void Report::exportCharts(json::Object&& out) {

	for (auto &&rec: tradeMap) {
		out.set(rec.first, rec.second);
	}
}

bool Report::OKeyCmp::operator ()(const OKey& a, const OKey& b) const {
	int cmp = a.symb.compare(b.symb);
	if (cmp == 0) {
		return a.dir < b.dir;
	} else {
		return cmp < 0;
	}
}

template<typename ME>
void Report::sendStreamInfo(ME &me, const std::string_view &symb, const json::Value &object) {
	me.sendStream(json::Object{
			{"type","info"},
			{"symbol",symb}},object);

}

void Report::setInfo(std::size_t rev, std::string_view symb, const InfoObj &infoObj) {

	if (rev != revize) return;

	std::string brokerImg;


	auto broker = dynamic_cast<IBrokerControl *>(infoObj.exchange.get());
	if (broker) {
		try {
			json::base64->encodeBinaryValue(json::map_str2bin(broker->getBrokerInfo().favicon),[&](std::string_view c){
				brokerImg.append(c);
			});
		} catch (...) {}

	}
	if (brokerImg.empty()) {
		brokerImg =
				"iVBORw0KGgoAAAANSUhEUgAAADAAAAAwBAMAAAClLOS0AAAAG1BMVEVxAAAAAQApKylNT0x8fnuX"
				"mZaztbLNz8z4+vcarxknAAAAAXRSTlMAQObYZgAAASFJREFUOMulkzFPxDAMhRMhmGNE72YGdhiQ"
				"boTtVkApN8KAriO3XPoDaPDPxk7SXpo6QogndfGnl/o5jlInaaNEwf0LSHX9ijhInhWS3gXDjoFg"
				"0Yi+Q1yCBvHuUjprjR5ghwcBDEZvRVBxrGr/SF3dLkHHObwQfc3gIM2KLF6cIiBemwqQx87AFKUo"
				"AkkJbLDQkAx9Cb7NL6BDl1ddBh4h01UGnvIm/wtSKso6B/SFVFu6kRFoYIBhpTgSgzCzG9svgH02"
				"6oxD8VFfp6NID+oi7jKAGX/ecOVTnQfHvF3Sm9J71y+AO5IfIAYM12ViwBQqgml9GOQjCQ/HC7Oq"
				"gvyoGZj2YwZqN/hhbWujWttm4I/rExvNNT6fxhWaRgeFuPYDghTP70Os5zoAAAAASUVORK5CYII=";
	}

	json::Value data = json::Object({
		{"title",infoObj.title},
		{"currency", infoObj.minfo.currency_symbol},
		{"asset", infoObj.minfo.asset_symbol},
		{"price_symb", infoObj.minfo.invert_price?infoObj.minfo.inverted_symbol:infoObj.minfo.currency_symbol},
		{"brokerIcon", json::String({"data:image/png;base64,",brokerImg})},
		{"brokerName", infoObj.brokerName},
		{"inverted", infoObj.minfo.invert_price},
		{"walletId", infoObj.minfo.wallet_id},
		{"emulated",infoObj.minfo.simulator},
		{"order", infoObj.order}
	});
	infoMap[symb] = data;
	sendStreamInfo(*this,symb, data);

}

template<typename ME>
void Report::sendStreamPrice(ME &me, const std::string_view &symb, double data) {
	me.sendStream(json::Object{
		{ "type", "price" },
		{ "symbol",symb }},data );
}

void Report::setPrice(std::size_t rev, std::string_view symb, double price) {

	if (rev != revize) return;

	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();

	double data = inverted?1.0/price:price;
	priceMap[symb] = data;

	sendStreamPrice(*this,symb, data);
}


void Report::exportOrders(json::Array &&out) {

	for (auto &&ord : orderMap) {
		if (ord.second.price) {
			out.push_back(Object({
					{"symb",ord.first.symb},
					{"dir",static_cast<int>(ord.first.dir)},
					{"size",ord.second.size},
					{"price",ord.second.price}
			}));
		}
	}
}

void Report::exportTitles(json::Object&& out) {
	for (auto &&rec: infoMap) {
			out.set(rec.first, rec.second);
	}
}

void Report::exportPrices(json::Object &&out) {
	for (auto &&rec: priceMap) {
			out.set(rec.first, rec.second);
	}
}

template<typename ME>
void Report::sendStreamError(ME &me, const std::string_view &symb, const json::Value &obj) {
	me.sendStream(json::Object {
		{"type", "error" },
		{"symbol",symb}},obj );
}

void Report::setError(std::size_t rev,std::string_view symb, const ErrorObj &errorObj) {
	if (rev != revize) return;

	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();

	Object obj;
	if (!errorObj.genError.empty()) obj.set("gen", errorObj.genError);
	if (!errorObj.buyError.empty()) obj.set(inverted?"sell":"buy", errorObj.buyError);
	if (!errorObj.sellError.empty()) obj.set(inverted?"buy":"sell", errorObj.sellError);
	errorMap[symb] = obj;

	sendStreamError(*this,symb, obj);
}

void Report::exportMisc(json::Object &&out) {
	for (auto &&rec: miscMap) {
			auto erritr = errorMap.find(rec.first);
			Value err = erritr == errorMap.end()?Value():erritr->second;
			out.set(rec.first, rec.second.replace("error", err));
	}
}

void Report::addLogLine(std::string_view ln) {
	logLines.push_back(std::string_view(ln));
	sendStream(json::Object{{"type","log"}},std::string_view(ln));
}

void Report::reportLogMsg(std::size_t rev, const std::string_view &symb, std::uint64_t timestamp, const std::string_view &text) {
	if (rev != revize) return;
	sendStream(json::Object{
		{"type","slog"},
		{"symbol", std::string_view(symb)}},
		json::Object {
			{"tm", timestamp},
			{"text", text},
		}
	);
}

using namespace ondra_shared;

class CaptureLog: public ondra_shared::StdLogProviderFactory {
public:
	CaptureLog(const ondra_shared::SharedObject<Report> &rpt, ondra_shared::PStdLogProviderFactory target):rpt(rpt),target(target) {}

	virtual void writeToLog(const StrViewA &line, const std::time_t &, LogLevel level) override;
	virtual bool isLogLevelEnabled(ondra_shared::LogLevel lev) const override;


protected:
	SharedObject<Report> rpt;
	ondra_shared::PStdLogProviderFactory target;
};

inline void CaptureLog::writeToLog(const StrViewA& line, const std::time_t&tm, LogLevel level) {
	if (level >= LogLevel::info) rpt.lock()->addLogLine(line);
	target->sendToLog(line, tm, level);
}

inline bool CaptureLog::isLogLevelEnabled(ondra_shared::LogLevel lev) const {
	return target->isLogLevelEnabled(lev);
}

ondra_shared::PStdLogProviderFactory Report::captureLog(const ondra_shared::SharedObject<Report> &rpt, ondra_shared::PStdLogProviderFactory target) {
	return new CaptureLog(rpt, target);
}

template<typename ME>
void Report::sendStreamMisc(ME &me, const std::string_view &symb, const json::Value &object) {
	me.sendStream(json::Object{
		{"type","misc"},
		{"symbol",std::string_view(symb)}},object
	);

}

void Report::setMisc(std::size_t rev, std::string_view symb, const MiscData &miscData, bool initial) {

	if (rev != revize) return;

	if (initial && miscMap.find(symb) != miscMap.end()) return;
	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();

	double spread;
	double lp = miscData.lastTradePrice * std::exp(-miscData.spread);
	double hp = miscData.lastTradePrice * std::exp(miscData.spread);
	if (inverted) {
		lp = 1.0/lp;
		hp = 1.0/hp;
	}
	spread = std::abs(hp-lp);



	Object output;
	output.setItems({{"ms", spread},
		{"mt",miscData.total_trades},
		{"tt",miscData.total_time},
		{"bt",miscData.budget_total},
		{"a",miscData.achieve_mode},
		{"accum",miscData.accumulated},
		{"ba",miscData.budget_assets},
		{"en",miscData.enabled},
		{"upnl",miscData.upnl},
		{"rpnl",miscData.rpnl},
	});


	output.set("be", miscData.budget_extra);

	if (inverted) {

		output.setItems({
			{"t",-miscData.trade_dir},
			{"pos",-miscData.position},
			{"Map", 1.0/miscData.equilibrium},
			{"ml",1.0/miscData.highest_price},
			{"mh",1.0/miscData.lowest_price},
			{"mdmb", miscData.dynmult_sell},
			{"mdms", miscData.dynmult_buy},
			{"cur_norm_buy",miscData.cur_norm_sell},
			{"cur_norm_sell",miscData.cur_norm_buy},
			{"op", 1.0/miscData.entry_price},
			{"ltp", 1.0/miscData.lastTradePrice}});
	} else {
		output.setItems({
			{"t",miscData.trade_dir},
			{"pos",miscData.position},
			{"mcp", miscData.equilibrium},
			{"ml",miscData.lowest_price},
			{"mh",miscData.highest_price},
			{"mdmb", miscData.dynmult_buy},
			{"mdms", miscData.dynmult_sell},
			{"cur_norm_buy",miscData.cur_norm_buy},
			{"cur_norm_sell",miscData.cur_norm_sell},
			{"op", miscData.entry_price},
			{"ltp", miscData.lastTradePrice}
		});
	}
	miscMap[symb] = output;
	sendStreamMisc(*this,symb, output);
}


void Report::clear() {
	revize++;
	tradeMap.clear();
	infoMap.clear();
	priceMap.clear();
	miscMap.clear();
	errorMap.clear();
	orderMap.clear();
	logLines.clear();
	refresh_after_clear= true;
}

void Report::perfReport(json::Value report) {
	perfRep = report;
	sendStream(Object{{"type","performance"}},perfRep);
}

void Report::StreamData::set_event(const json::Value &hdr, const json::Value &data) {
	std::hash<json::Value> h;
	command = false;
	hdr_hash = h(hdr.stripKey());
	data_hash = h(data.stripKey());
	event = hdr;
	event.setItems({{"data", data}});

}

void Report::sendStream(const json::Value &hdr, const json::Value &data) {
	if (refresh_after_clear) return;
	StreamData sdata;
	sdata.set_event(hdr, data);
	auto iter = std::remove_if(streams.begin(), streams.end(), [&](const auto &s){
		return !s(sdata);
	});
	streams.erase(iter, streams.end());
}

template<typename ME>
void Report::sendStreamGlobal(ME &me) const {
	me.sendStream(Object{
		{"type","config"}},
		Object{
			{"interval", interval_in_ms},
		}
	);
	me.sendStream(Object{
		{"type","performance"}},
		perfRep
	);
	me.sendStream(Object{
		{"type","version"}},
		MMBOT_VERSION
	);
}

void Report::pingStreams() {
	if (!streams.empty()) {
		auto iter = std::remove_if(streams.begin(), streams.end(), [&](const auto &s){
			return !s(ev_ping);
		});
		streams.erase(iter, streams.end());
	}
}

std::size_t Report::initCounter() {
	return time(nullptr);
}

void Report::addStream(Stream &&stream) {
	if (refresh_after_clear) {
		this->streams.push_back(std::move(stream));
	} else if (stream(ev_refresh) && stream_refresh(stream) && stream(ev_end_refresh)) {
		this->streams.push_back(std::move(stream));
	}
}

json::Value Report::OValue::toJson() const {
	return json::Object({
		{"price",price},
		{"size",size}
	});
}

void Report::setNewsMessages(unsigned int count) {
	newsMessages = count;
	sendNewsMessages(*this);


}
template<typename ME>
void Report::sendNewsMessages(ME &me) const {
	me.sendStream(
			json::Object{
				{"type","news"}},
				json::Object{
				 {"count", newsMessages}
				}

			);

}
template<typename ME>
void Report::sendLogMessages(ME &me) const {
	for (json::Value ln: logLines) {
		me.sendStream(json::Object{{"type","log"}},ln);
	}
}

bool Report::stream_refresh(Stream &stream) const  {
	class Helper {
	public:
		Helper(Stream &stream):stream(stream) {}
		void sendStream(const Value &hdr, const Value &data) {
			StreamData dt;
			dt.set_event(hdr, data);
			ok = ok && stream(dt);
		}
		Stream &stream;
		bool ok = true;
	};

	Helper hlp(stream);
	sendStreamGlobal(hlp);
	for (const auto &item: infoMap) {
		sendStreamInfo(hlp,item.first, item.second);
	}
	for (const auto &item: tradeMap) {
		sendStreamTrades(hlp,item.first, item.second);
	}
	for (const auto &item: miscMap) {
		sendStreamMisc(hlp,item.first, item.second);
	}
	for (const auto &item: errorMap) {
		sendStreamError(hlp,item.first, item.second);
	}
	for (const auto &item: priceMap) {
		sendStreamPrice(hlp,item.first, item.second);
	}
	for (const auto &item: orderMap) {
		sendStreamOrder(hlp,item.first, item.second);
	}
	sendNewsMessages(hlp);
	sendLogMessages(hlp);
	return hlp.ok;
}
