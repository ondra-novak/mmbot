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

#include "../shared/linear_map.h"
#include "../shared/logOutput.h"
#include "../shared/range.h"
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

inline double pow2(double x) {
	return x*x;
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
		//guess initial fiat to achieve balance
		double init_fiat = (t.balance+t.eff_size)*t.eff_price;
		//so the first trade doesn't change the value of portfolio
//		double init_value = init_balance*t.eff_price+init_fiat;
		//
		double init_price = t.eff_price;

		double prev_balance = init_balance;
		double prev_price = init_price;
		double prev_fiat = init_fiat;
		double ass_sum = 0;
		double cur_sum = 0;
		double cur_fromPos = 0;
		double cur_fromHodl = 0;
//		double prev_value = init_value;
		double norm = 0;

		while (iter != tend) {

			auto &&t = *iter;

			double gain = (t.eff_price - prev_price)*ass_sum ;

			cur_fromPos += gain;
			cur_fromHodl += (t.eff_price - prev_price)*prev_balance;
			ass_sum += t.eff_size;
			cur_sum += t.eff_price;

			double b = prev_balance+t.eff_size;
			double f = prev_fiat-t.eff_size*t.eff_price;

//			double value = b*t.eff_price + f;
			norm = f - b*t.eff_price;


			prev_balance = t.balance;
//			prev_value = value;
			prev_price = t.eff_price;
			prev_fiat = f;

			if (t.time >= first) {
				records.push_back(Object
						("id", t.id)
						("time", t.time)
						("achg", t.eff_size)
						("gain", gain)
						("norm", norm)
						("pos", ass_sum)
						("pl", cur_fromPos)
						("hodl", cur_fromHodl)
						("price", t.price)
						("volume", -t.eff_price*t.eff_size)
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
