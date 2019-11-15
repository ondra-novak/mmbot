/*
 * tradingengine.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include "tradingengine.h"


TradingEngine::TradingEngine(Command &&cmdFn)
	:cmdFn(std::move(cmdFn))
{
	uidcnt = now();
}


void TradingEngine::start(RegisterPriceChangeEvent &&regFn) {
	Sync _(lock);
	starter = std::move(regFn);
	stopped = true;
}

void TradingEngine::startListenPrices() {
	Sync _(lock);
	starter([me = PTradingEngine(this)](const IStockApi::Ticker &price) {
		Sync _(me->lock);
		if (me->orders.empty()) {
			me->stopped =true;
			return false;
		}
		me->onPriceChange(price);
		return true;
	});
	stopped = false;
}

IStockApi::Ticker TradingEngine::getTicker() const {
	Sync _(lock);
	return ticker;
}

void TradingEngine::readOrders(std::function<void(IStockApi::Order)> &&cb) {
	Sync _(lock);
	for (auto &&o : orders) {
		cb(o);
	}
}

TradingEngine::UID TradingEngine::readTrades(UID fromId, std::function<void(IStockApi::Trade)> &&cb) {
	Sync _(lock);
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

TradingEngine::UID TradingEngine::placeOrder(double price, double size, json::Value clientId) {
	Sync _(lock);
	if (stopped) startListenPrices();
	UID oid = uidcnt++;
	orders.push_back(Order {
		oid,
		clientId,
		price,
		size
	});
	updateMinMaxPrice();
	return oid;
}

void TradingEngine::cancelOrder(UID id) {
	Sync _(lock);
	auto iter = std::find_if(orders.begin(), orders.end(), [&](const Order &o) {
		return o.id.getUInt() == id;
	});
	if (iter != orders.end()) orders.erase(iter);
}

void TradingEngine::onPriceChange(const IStockApi::Ticker &price) {

	if (price.ask < minPrice || price.bid > maxPrice) {
		std::vector<Order> pending;
		pending.reserve(orders.size());
		double volume = 0;
		for (auto &&order: orders) {
			if ((order.size < 0 && order.price < price.bid)
					|| (order.size > 0 && order.price > price.ask)) {
				volume += order.size;
			} else {
				pending.push_back(order);
			}
		}
		if (volume) {
			double execprice = cmdFn(volume);
			trades.push_back(Trade {
				uidcnt++,
				execprice,
				volume,
				now()
			});
		}
	}
	updateMinMaxPrice();
}

void TradingEngine::updateMinMaxPrice() {
	if (orders.empty()) {
		minPrice = 0;
		maxPrice = std::numeric_limits<double>::max();
	} else {
		minPrice = maxPrice = orders[0].price;
		for (auto &&o: orders) {
			minPrice = std::min(minPrice, o.price);
			maxPrice = std::max(maxPrice, o.price);
		}
	}
}

void TradingEngine::stop() {
	Sync _(lock);
	orders.clear();
}

PTradingEngine TradingEngine::create(Command &&cmdIfc) {
	return new TradingEngine(std::move(cmdIfc));
}


std::uint64_t TradingEngine::now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}
