/*
 * cmdcntr.h
 *
 *  Created on: 14. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_SIMPLEFX_FNDEF_H_
#define SRC_SIMPLEFX_FNDEF_H_

#include <functional>

#include "../main/istockapi.h"

///Buys or sells an underlying instrument
/** To buy, use positive value
 *  To sell, use negative value
 *
 *  Returns execution price
 *
 *  On CFD market, it should close opened oposite positions (FIFO mode)
 */
using Command = std::function<double(double)>;

///Called on price change
/**
 * @param price new price
 * @retval true - continue
 * @retval false - stop
 */
using OnPriceChange = std::function<bool(const IStockApi::Ticker &price)>;


///Called to start listening price changes
/**
 * @param cb callback
 */
using RegisterPriceChangeEvent = std::function<void(OnPriceChange &&cb)>;

enum class SettlementMode {
	///no settlement
	none,
	///settlement is active (manually activated)
	active,
	///settlement is active on friday before the market is closed
	friday
};


#endif /* SRC_SIMPLEFX_FNDEF_H_ */
