/*
 * tradingengine.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include "tradingengine.h"

#include <cmath>


#include "../shared/logOutput.h"

using ondra_shared::logDebug;
using ondra_shared::logWarning;

TradingEngine::TradingEngine(Command &&cmdFn)
	:cmdFn(std::move(cmdFn)),ticker{0,0,0,0}
{
	uidcnt = now();
}


void TradingEngine::start(RegisterPriceChangeEvent &&regFn) {
	Sync _(lock);
	starter = std::move(regFn);
}

void TradingEngine::startListenPrices() const {
	Sync _(lock);
	starter([me = PTradingEngine(const_cast<TradingEngine *>(this))](const IStockApi::Ticker &price) {
		Sync _(me->lock);
		if (me->orders.empty() && me->quoteStop < now()) {
			me->quotesStopped=true;
			return false;
		}
		me->onPriceChange(price);
		return true;
	});
	quotesStopped = false;
}

IStockApi::Ticker TradingEngine::getTicker() const {
	Sync _(lock);
	runQuotes();
	tickerWait.wait(_,[&] {return ticker.time>0;});
	return ticker;
}

void TradingEngine::readOrders(std::function<void(IStockApi::Order)> &&cb) {
	Sync _(lock);
	runQuotes();
	for (auto &&o : orders) {
		cb(o);
	}
}

TradingEngine::UID TradingEngine::readTrades(UID fromId, std::function<void(IStockApi::Trade)> &&cb) {
	Sync _(lock);
	runQuotes();
	UID last = fromId;
	for (auto &&t : trades) {
		if (t.id > fromId) {cb(IStockApi::Trade {
			t.id,
			t.timestamp,
			t.size,
			t.price,
			t.size,
			t.price
		});
		last = t.id;
		}
	}
	return last;
}

TradingEngine::UID TradingEngine::placeOrder(double price, double size, json::Value clientId,const UID *replace ) {
	Sync _(lock);
	runQuotes();
	if (replace) cancelOrder(*replace);
	if (size) {
		UID oid = uidcnt++;
		orders.push_back(Order {
			oid,
			clientId,
			size,
			price
		});
		logDebug("Order placed $1 at price $2 size $3", oid, price, size);
		updateMinMaxPrice();
		return oid;
	} else {
		return 0;
	}
}

void TradingEngine::cancelOrder(UID id) {
	Sync _(lock);
	auto iter = std::find_if(orders.begin(), orders.end(), [&](const Order &o) {
		return o.id.getUInt() == id;
	});
	if (iter != orders.end()) {
		logDebug("Order canceled: $1", id);
		orders.erase(iter);
	} else {
		logWarning("Order cancel not found: $1", id);
	}
}

void TradingEngine::onPriceChange(const IStockApi::Ticker &price) {

	ticker.ask = std::min(maxPrice, price.ask);
	ticker.bid = std::max(minPrice, price.bid);
	ticker.last = sqrt(ticker.ask*ticker.bid);
	ticker.time = price.time;

	logDebug("Quote received: ask: $1, bid $2", ticker.ask, ticker.bid);

	if (ticker.ask < minPrice || ticker.bid > maxPrice) {
		std::vector<Order> pending;
		pending.reserve(orders.size());
		double volume = 0;
		for (auto &&order: orders) {
			if ((order.size < 0 && order.price < ticker.bid)
					|| (order.size > 0 && order.price > ticker.ask)) {
				volume += order.size;
				logDebug("Matching order $1 at $2 size $3", order.id.getUInt(), order.price, order.size);
			} else {
				pending.push_back(order);
			}
		}
		if (volume) {
			double execprice = cmdFn(volume);
			logDebug("Executed total $1 at price $2", volume, execprice);
			trades.push_back(Trade {
				uidcnt++,
				execprice,
				volume,
				now()
			});
		}
		std::swap(pending, orders);
		updateMinMaxPrice();
	}
	tickerWait.notify_all();
}

void TradingEngine::updateMinMaxPrice() {
	minPrice = 0;
	maxPrice = std::numeric_limits<double>::max();
	for (auto &&o: orders) {
		if (o.size > 0) minPrice = std::max(minPrice, o.price);
		else maxPrice = std::min(maxPrice,o.price);
	}
	logDebug("Spread range updated: $1 - $2", minPrice, maxPrice);
}

void TradingEngine::stop() {
	Sync _(lock);
	orders.clear();
	quoteStop = 0;
}

PTradingEngine TradingEngine::create(Command &&cmdIfc) {
	return new TradingEngine(std::move(cmdIfc));
}


std::uint64_t TradingEngine::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}

void TradingEngine::runQuotes() const {
	if (quotesStopped) startListenPrices();
	quoteStop = now()+(5*60000);
}
