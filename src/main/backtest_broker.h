/*
 * backtest_broker.h
 *
 *  Created on: 15. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_BACKTEST_BROKER_H_
#define SRC_MAIN_BACKTEST_BROKER_H_
#include <memory>

#include <shared/shared_object.h>

#include "abstractExtern.h"

using std::shared_ptr;


class BacktestBroker: public AbstractExtern {
public:

	using AbstractExtern::AbstractExtern;

	json::Value get_symbols() {
		return jsonRequestExchange("symbols",json::Value());
	}

	json::Value get_minute(const std::string_view &asset, const std::string_view &currency, std::uint64_t from) {
		return jsonRequestExchange("minute", json::Value(json::object, {
				json::Value("asset",asset),
				json::Value("currency",currency),
				json::Value("from",from)
		}));
	}

	virtual ~BacktestBroker() {}
};

using PBacktestBroker = std::unique_ptr<BacktestBroker>;


#endif /* SRC_MAIN_BACKTEST_BROKER_H_ */
