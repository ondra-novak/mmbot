/*
 * tradingengine.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include "tradingengine.h"
#include <imtjson/string.h>
#include <cmath>
#include "lexid.h"


#include <shared/logOutput.h>

using ondra_shared::logDebug;
using ondra_shared::logError;
using ondra_shared::logNote;
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
		me->onPriceChange(price, _);
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

std::string TradingEngine::readTrades(const std::string &fromId, std::function<void(IStockApi::Trade)> &&cb) {
	Sync _(lock);
	runQuotes();
	auto last = fromId;
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

template<typename T>
std::string UIDToString(T val) {
	return lexID::create(val);
}

std::string TradingEngine::placeOrder(double price, double size, json::Value clientId,const std::string *replace ) {
	Sync _(lock);
	runQuotes();
	if (replace) cancelOrder(*replace);
	if (size) {
		std::string oid = UIDToString(uidcnt++);
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

void TradingEngine::cancelOrder(std::string id) {
	Sync _(lock);
	auto iter = std::find_if(orders.begin(), orders.end(), [&](const Order &o) {
		return o.id.getString() == id;
	});
	if (iter != orders.end()) {
		logDebug("Order canceled: $1", id);
		orders.erase(iter);
	} else {
		logWarning("Order cancel not found: $1", id);
	}
}

void TradingEngine::executeTrade(double volume, double price) {
	try {
		double execprice = cmdFn(volume, price);
		if (execprice <= 0) return;
		Sync _(lock);
		logDebug("Executed total $1 at price $2", volume, execprice);
		trades.push_back(Trade { UIDToString(uidcnt++), execprice, volume, now() });
	} catch (std::exception &e) {
		logError("Execution failed: $1", e.what());
	}
}

void TradingEngine::addTrade(double volume) {
	Sync _(lock);
	logDebug("Added trade: price=$1, size=$2", ticker.last, volume);
	trades.push_back(Trade { UIDToString(uidcnt++), ticker.last, volume, now() });
}

void TradingEngine::onPriceChange(const IStockApi::Ticker &price, Sync &hlck) {

	double volume = 0;

	ticker.ask = std::min(maxPrice, price.ask);
	ticker.bid = std::max(minPrice, price.bid);
	ticker.last = sqrt(ticker.ask*ticker.bid);
	ticker.time = price.time;


	logDebug("Quote received: ask: $1, bid $2", ticker.ask, ticker.bid);

	if (ticker.ask < minPrice || ticker.bid > maxPrice) {
		std::vector<Order> pending;
		pending.reserve(orders.size());
		for (auto &&order: orders) {
			if ((order.size < 0 && order.price < ticker.bid)
					|| (order.size > 0 && order.price > ticker.ask)) {
				volume += order.size;
				logDebug("Matching order $1 at $2 size $3", order.id.toString().str(), order.price, order.size);
			} else {
				pending.push_back(order);
			}
		}
		std::swap(pending, orders);
		updateMinMaxPrice();
	}
	tickerWait.notify_all();

	if (volume) {
		//to avoid deadlock, we can now unlock
		//while trade is being executed, no reason to block user from doing other requests
		hlck.unlock();
		executeTrade(volume, ticker.last);
	}
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

void TradingEngine::runSettlement(double amount) {
	Sync _(lock);
	logNote("Settlement $1", amount);
	orders.clear();
	executeTrade(amount, ticker.last);
}
