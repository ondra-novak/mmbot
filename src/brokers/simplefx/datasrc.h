/*
 * datasrc.h
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_DATASRC_H_
#define SRC_SIMPLEFX_DATASRC_H_

#include <functional>
#include <string_view>

using ReceiveQuotesFn = std::function<bool(std::string_view symbol, double bid, double ask, std::uint64_t time)>;

using SubscribeFn = std::function<void(std::string_view symbol)>;



#endif /* SRC_SIMPLEFX_DATASRC_H_ */
