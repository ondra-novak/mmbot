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

void Report::setTrades(StrViewA symb, double current_balance, StringView<IStockApi::Trade> trades) {

	using ondra_shared::range;

	json::Array records;



	if (!trades.empty()) {

		auto trange = range(trades.begin()+1, trades.end());

		double delta_assets = 0;
		for (const IStockApi::Trade &t : trange) delta_assets+= t.eff_size;
		double init_assets = current_balance - delta_assets;


		std::size_t last = trades[trades.length-1].time;
		std::size_t first = last - interval_in_ms;


		double last_price = trades[0].eff_price;
		double pl = 0;
		double assets = 0;
		double volsum = 0;
		double init_price = trades[0].eff_price;
		double init_fiat = init_price * init_assets;
//		double init_val = init_fiat + init_price*init_assets;


		for (const IStockApi::Trade &t : trange) {

			double volume = t.eff_price*t.eff_size ;
			double gain = (t.eff_price - last_price)*assets ;
//			double prev_assets = assets;

			pl += gain;
			last_price = t.eff_price;
			assets += t.eff_size;
			volsum -= volume;

			double vbal = 2*init_assets*sqrt(init_price * t.eff_price);
			double vcur = (init_assets + assets) * t.eff_price + init_fiat + volsum;
			double norm = vcur - vbal;

			double rel = assets * t.eff_price + volsum;

			if (t.time >= first) {
				records.push_back(Object
						("id", t.id)
						("time", t.time)
						("achg", t.eff_size)
						("gain", gain)
						("norm", norm)
						("rel", rel)
						("pos", assets)
						("pl", pl)
						("price", t.price)
						("volume", -t.eff_price*t.eff_size)
				);
			}

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
