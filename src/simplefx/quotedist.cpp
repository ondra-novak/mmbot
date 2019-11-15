/*
 * quotedist.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include "quotedist.h"

#include <cmath>
RegisterPriceChangeEvent QuoteDistributor::createRegFn(const std::string_view &symbol) {
	return [me = PQuoteDistributor(this), symbol = std::string(symbol)](OnPriceChange &&fn) {
		me->subscribe(symbol, std::move(fn));
	};
}

ReceiveQuotesFn QuoteDistributor::createReceiveFn() {
	return [me = PQuoteDistributor(this)](std::string_view symbol, double bid, double ask, std::uint64_t time){
		return me->receiveQuotes(symbol,bid,ask,time);
	};
}

void QuoteDistributor::connect(SubscribeFn &&subfn) {
	Sync _(lock);
	this->subfn = std::move(subfn);
}

bool QuoteDistributor::receiveQuotes(const std::string_view &symbol, double bid, double ask, std::uint64_t time) {
	Sync _(lock);
	std::string s(symbol);
	auto r = listeners.equal_range(s);
	if (r.first == r.second) {
		return false;
	}

	IStockApi::Ticker tk{
			bid,
			ask,
			sqrt(bid*ask),
			time
		};

	for (auto x = r.first; x != r.second;) {
		if (!(*x).second(tk)) {
			x = listeners.erase(x);
		} else {
			++x;
		}
	}
	return true;
}

void QuoteDistributor::subscribe(const std::string_view &symbol, OnPriceChange &&listener) {
	Sync _(lock);

	std::string s (symbol);

	auto r = listeners.equal_range(s);
	if (r.first == r.second) {
		subfn(symbol);
	}

	listeners.emplace(std::move(s), std::move(listener));
}

void QuoteDistributor::disconnect() {
	subfn = nullptr;
}
