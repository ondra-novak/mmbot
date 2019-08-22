/*
 * backtest_broker.cpp
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */


#include "backtest_broker.h"


BacktestBroker::BacktestBroker(ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MarketInfo &minfo, double balance)
	:chart(chart),minfo(minfo),balance(balance),initial_balance(balance) {
	pos = chart.length;
	back = true;
}

BacktestBroker::TradeHistory BacktestBroker::getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {
	return TradeHistory(trades.begin()+lastId.getUInt(), trades.end());
}


BacktestBroker::Orders BacktestBroker::getOpenOrders(const std::string_view & par) {
	Orders ret;
	if (!buy_ex) {
		ret.push_back(buy);
	}
	if (!sell_ex) {
		ret.push_back(sell);
	}
	return ret;
}

BacktestBroker::Ticker BacktestBroker::getTicker(const std::string_view & piar) {
	auto tm = chart[pos].time;
	if (back) {
		tm = 2*chart[0].time-tm;
	}

	return Ticker {
		chart[pos].bid,
		chart[pos].ask,
		chart[pos].last,
		tm
	};
}

json::Value BacktestBroker::placeOrder(const std::string_view & ,
		double size, double price,json::Value clientId,
		json::Value ,double ) {

	Order ord{0,clientId, size, price};
	if (size < 0) {
		sell = ord;
		sell_ex = false;
	} else {
		buy = ord;
		buy_ex = false;
	}

	return 1;
}



bool BacktestBroker::reset() {
	auto nx = pos+(back?-1:1);
	if (nx < 0) {
		back = false;
		return reset();
	} else if (static_cast<unsigned int>(nx) >= chart.length) {
		return false;
	}

	pos = nx;
	const IStatSvc::ChartItem &p = chart[pos];

	auto txid = trades.size()+1;
	auto tm = p.time;
	if (back) {
		tm = 2*chart[0].time-tm;
	}

	if (p.bid > sell.price && !sell_ex) {
		Trade tr;
		tr.eff_price = sell.price;
		tr.eff_size = sell.size;
		tr.id = txid;
		tr.price = sell.price;
		tr.size = sell.size;
		tr.time = tm;
		minfo.removeFees(tr.eff_size, tr.eff_price);
		sell_ex = true;
		trades.push_back(tr);
		balance += tr.eff_size;
		currency -= tr.eff_size*tr.eff_price;
		sells++;
	}
	if (p.ask < buy.price && !buy_ex) {
		Trade tr;
		tr.eff_price = buy.price;
		tr.eff_size = buy.size;
		tr.id = txid;
		tr.price = buy.price;
		tr.size = buy.size;
		tr.time = tm;
		minfo.removeFees(tr.eff_size, tr.eff_price);
		buy_ex = true;
		trades.push_back(tr);
		balance += tr.eff_size;
		currency -= tr.eff_size*tr.eff_price;
		buys++;
	}

	return true;

}

double BacktestBroker::getBalance(const std::string_view & x) {
	return balance;
}

