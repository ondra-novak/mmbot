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

#include "../shared/linear_map.h"
#include "../shared/logOutput.h"
#include "../shared/range.h"
#include "../shared/stdLogOutput.h"
#include "sgn.h"
#include "../../version.h"
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

void Report::genReport() {
	while (logLines.size()>30) logLines.erase(logLines.begin());
	counter++;
	report->store(genReport_noStore());
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
		{ "dir", key.dir },
		{ "data", data.toJson()}
	});
}

void Report::setOrders(StrViewA symb, int n, const std::optional<IStockApi::Order> &buy,
	  	  	  	  	  	  	  	     const std::optional<IStockApi::Order> &sell) {
	const json::Value &info = infoMap[symb];
	bool inverted = info["inverted"].getBool();

	int buyid = inverted?-n:n;

	OKey buyKey {symb, buyid};
	OKey sellKey {symb, -buyid};
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
			{ "id", rw["iid"]},
			{ "data",rw.replace("iid",json::undefined) }
		});
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

void Report::setTrades(StrViewA symb, double finalPos, StringView<IStatSvc::TradeRecord> trades) {

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
			{"symbol",symb},
			{"data",object}});

}

void Report::setInfo(StrViewA symb, const InfoObj &infoObj) {
	json::Value data = json::Object({
		{"title",infoObj.title},
		{"currency", infoObj.currencySymb},
		{"asset", infoObj.assetSymb},
		{"price_symb", infoObj.priceSymb},
		{"brokerIcon", infoObj.brokerIcon},
		{"brokerName", infoObj.brokerName},
		{"inverted", infoObj.inverted},
		{"walletId", infoObj.walletId},
		{"emulated",infoObj.emulated},
		{"order", infoObj.order}
	});
	infoMap[symb] = data;
	sendStreamInfo(*this,symb, data);

}

template<typename ME>
void Report::sendStreamPrice(ME &me, const std::string_view &symb, double data) {
	me.sendStream(json::Object{
		{ "type", "price" },
		{ "symbol",symb },
		{ "data", data } });
}

void Report::setPrice(StrViewA symb, double price) {

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
		{"symbol",symb},
		{ "data", obj } });
}

void Report::setError(StrViewA symb, const ErrorObj &errorObj) {

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

void Report::addLogLine(StrViewA ln) {
	logLines.push_back(std::string_view(ln));
	sendStream(json::Object({{"type","log"},{"data",std::string_view(ln)}}));
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
	me.sendStream(json::Object({
		{"type","misc"},
		{"symbol",std::string_view(symb)},
		{"data",object}
	}));

}

void Report::setMisc(StrViewA symb, const MiscData &miscData, bool initial) {

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


	if (miscData.budget_extra.has_value())
		output.set("be", *miscData.budget_extra);

	if (inverted) {

		output.setItems({
			{"t",-miscData.trade_dir},
			{"pos",-miscData.position},
			{"mcp", 1.0/miscData.calc_price},
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
			{"mcp", miscData.calc_price},
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

void Report::clear(StrViewA symb) {
	tradeMap.erase(symb);
	infoMap.erase(symb);
	priceMap.erase(symb);
	miscMap.erase(symb);
	errorMap.erase(symb);
	orderMap.clear();
}

void Report::clear() {
	tradeMap.clear();
	infoMap.clear();
	priceMap.clear();
	miscMap.clear();
	errorMap.clear();
	orderMap.clear();
	logLines.clear();
}

void Report::perfReport(json::Value report) {
	perfRep = report;
}

void Report::sendStream(const json::Value &v) {
	auto iter = std::remove_if(streams.begin(), streams.end(), [&](const auto &s){
		return !s(v);
	});
	streams.erase(iter, streams.end());
}

template<typename ME>
void Report::sendStreamGlobal(ME &me) const {
	me.sendStream(Object{
		{"type","config"},
		{"data",Object{
			{"interval", interval_in_ms},
		}}
	});
	me.sendStream(Object{
		{"type","performance"},
		{"data", perfRep}
	});
	me.sendStream(Object{
		{"type","version"},
		{"data", MMBOT_VERSION}
	});
}

void Report::pingStreams() {
	if (!streams.empty()) {
		sendStream("ping");
		sendStreamGlobal(*this);
	}
}

std::size_t Report::initCounter() {
	return time(nullptr);
}

void Report::addStream(Stream &&stream) {
	if (stream("refresh") && stream_refresh(stream) && stream("end_refresh")) {
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
				{"type","news"},
				{"data",json::Object{
					{"count", newsMessages}
				}}

			});

}
template<typename ME>
void Report::sendLogMessages(ME &me) const {
	for (json::Value ln: logLines) {
		me.sendStream(json::Object({{"type","log"},{"data",ln}}));
	}
}

bool Report::stream_refresh(Stream &stream) const  {
	class Helper {
	public:
		Helper(Stream &stream):stream(stream) {}
		void sendStream(const Value &x) {
			ok = ok && stream(x);
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
