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

using ondra_shared::logError;
using std::chrono::_V2::system_clock;

using namespace json;

void Report::genReport() {

	Object st;
	exportCharts(st.object("charts"));
	exportOrders(st.array("orders"));
	exportTitles(st.object("info"));
	exportPrices(st.object("prices"));
	st.set("interval", interval_in_ms);
	st.set("time", std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
				   ).count());
	st.set("log", logLines);
	while (logLines.size()>30) logLines.erase(0);
	report->store(st);
}


struct ChartData {
	Array records;
	double last_price = 0;
	double sums = 0;
	double assets = 0;
	double init_price = 0;
};

void Report::setOrders(StrViewA symb, const std::optional<IStockApi::Order> &buy,
	  	  	  	  	  	  	  	     const std::optional<IStockApi::Order> &sell) {
	OKey buyKey {symb, 1};
	OKey sellKey {symb, -1};

	if (buy.has_value()) {
		orderMap[buyKey] = {buy->price, buy->size};
	} else{
		orderMap[buyKey] = {0, 0};
	}

	if (sell.has_value()) {
		orderMap[sellKey] = {sell->price, sell->size};
	} else {
		orderMap[sellKey] = {0, 0};
	}


}


void Report::setTrades(StrViewA symb, StringView<IStockApi::TradeWithBalance> trades) {

	using ondra_shared::range;

	json::Array records;



	if (!trades.empty()) {

		std::size_t last = trades[trades.length-1].time;
		std::size_t first = last - interval_in_ms;


		auto tend = trades.end();
		auto iter = trades.begin();
		auto &&t = *iter;

		//guess initial balance by substracting size
		double init_balance = (t.balance-t.eff_size);
		auto invest = std::accumulate(trades.begin()+1, trades.end(), std::make_pair(0.0,0.0),
				[](auto &&a, auto &&b){
					double assdiff = b.balance - a.first;
					double robot_trade = b.manual_trade?0:b.eff_size;
					double netto_diff = assdiff - robot_trade;
					double cost = netto_diff * b.eff_price;
					return std::make_pair(b.balance, a.second+cost);
		}).second;
		std::size_t invest_beg_time = t.time;

		//so the first trade doesn't change the value of portfolio
//		double init_value = init_balance*t.eff_price+init_fiat;
		//
		double init_price = t.eff_price;


		double prev_balance = init_balance;
		double prev_price = init_price;
		double ass_sum = 0;
		double cur_sum = 0;
		double cur_fromPos = 0;
		double norm_sum_ass = 0;
		double norm_sum_cur = 0;


		while (iter != tend) {

			auto &&t = *iter;

			double gain = (t.eff_price - prev_price)*ass_sum ;
			double earn = -t.eff_price * t.eff_size;


			double calcbal = prev_balance * sqrt(prev_price/t.eff_price);
			double asschg = calcbal - prev_balance;
			double curchg = calcbal * t.eff_price -  prev_balance * prev_price;
			if (iter != trades.begin() && !iter->manual_trade) {
				cur_fromPos += gain;
				ass_sum += t.eff_size;
				cur_sum += earn;

				norm_sum_ass += t.eff_size - asschg;
				norm_sum_cur += earn - curchg;
			}
			double norm = norm_sum_ass * t.eff_price + norm_sum_cur;

			prev_balance = t.balance;
			prev_price = t.eff_price;

			double invest_time = t.time - invest_beg_time;
			double app = ((norm/invest_time)*(365.0*24.0*60.0*60.0*1000.0)*100.0)/invest;
			if (!std::isfinite(app)) app = 0;

			if (t.time >= first) {
				records.push_back(Object
						("id", t.id)
						("time", t.time)
						("achg", t.eff_size)
						("gain", gain)
						("norm", norm)
						("nacum", norm_sum_ass)
						("pos", ass_sum)
						("pl", cur_fromPos)
						("price", t.price)
						("app", app)
						("volume", -t.eff_price*t.eff_size)
						("man",t.manual_trade)
				);
			}


			++iter;
		}

	}
	tradeMap[symb] = records;
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

void Report::setInfo(StrViewA symb,StrViewA title, StrViewA assetSymb, StrViewA currencySymb, bool emulated) {
	titleMap[symb] = Object
			("title",title)
			("currency", currencySymb)
			("asset", assetSymb)
			("emulated",emulated);
}

void Report::setPrice(StrViewA symb, double price) {
	priceMap[symb] = price;
}


void Report::exportOrders(json::Array &&out) {
	for (auto &&ord : orderMap) {
		if (ord.second.size) {
			out.push_back(Object
					("symb",ord.first.symb)
					("dir",static_cast<int>(ord.first.dir))
					("size",ord.second.size)
					("price",ord.second.price)
			);
		}
	}
}

void Report::exportTitles(json::Object&& out) {
	for (auto &&rec: titleMap) {
			out.set(rec.first, rec.second);
		}
}

void Report::exportPrices(json::Object &&out) {
	for (auto &&rec: priceMap) {
			out.set(rec.first, rec.second);
		}
}

void Report::addLogLine(StrViewA ln) {
	logLines.push_back(ln);
}

using namespace ondra_shared;

class CaptureLog: public ondra_shared::StdLogProviderFactory {
public:
	CaptureLog(Report &rpt, ondra_shared::PStdLogProviderFactory target):rpt(rpt),target(target) {}

	virtual void writeToLog(const StrViewA &line, const std::time_t &, LogLevel level) override;
	virtual bool isLogLevelEnabled(ondra_shared::LogLevel lev) const override;


protected:
	Report &rpt;
	ondra_shared::PStdLogProviderFactory target;
};

inline void CaptureLog::writeToLog(const StrViewA& line, const std::time_t&tm, LogLevel level) {
	if (level >= LogLevel::info) rpt.addLogLine(line);
	target->sendToLog(line, tm, level);
}

inline bool CaptureLog::isLogLevelEnabled(ondra_shared::LogLevel lev) const {
	return target->isLogLevelEnabled(lev);
}

ondra_shared::PStdLogProviderFactory Report::captureLog(ondra_shared::PStdLogProviderFactory target) {
	return new CaptureLog(*this, target);
}
