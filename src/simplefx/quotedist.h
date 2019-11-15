/*
 * quotedist.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_QUOTEDIST_H_
#define SRC_SIMPLEFX_QUOTEDIST_H_

#include <mutex>
#include <unordered_map>
#include "datasrc.h"
#include "fndef.h"

class QuoteDistributor;
using PQuoteDistributor = ondra_shared::RefCntPtr<QuoteDistributor>;

class QuoteDistributor: public ondra_shared::RefCntObj {
public:

	RegisterPriceChangeEvent createRegFn(const std::string_view &symbol);
	ReceiveQuotesFn createReceiveFn();
	void connect(SubscribeFn &&subfn);
	void disconnect();


protected:

	std::recursive_mutex lock;
	using Sync = std::unique_lock<std::recursive_mutex>;

	SubscribeFn subfn;

	std::unordered_multimap<std::string, OnPriceChange> listeners;

	bool receiveQuotes(const std::string_view &symbol, double bid, double ask, std::uint64_t time);
	void subscribe(const std::string_view &symbol, OnPriceChange &&listener);


};

#endif /* SRC_SIMPLEFX_QUOTEDIST_H_ */
