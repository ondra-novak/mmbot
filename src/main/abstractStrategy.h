/*
 * abstractStrategy.h
 *
 *  Created on: 11. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ABSTRACTSTRATEGY_H_
#define SRC_MAIN_ABSTRACTSTRATEGY_H_
#include <shared/refcnt.h>

#include "istockapi.h"

//new strategy interface - however, not clean interface, rather abstract object

class AbstractStrategy2;

using PStrategy2 = ondra_shared::RefCntPtr<const AbstractStrategy2>;


class AbstractStrategy2: public ondra_shared::RefCntObj{
public:

	///market state - constants
	struct MarketState {
		///market info
		const IStockApi::MarketInfo &minfo;
		///last ticker
		const IStockApi::Ticker &ticker;
		///last executed trade (if there is no last trade, it contains alert)
		const IStockApi::Trade &lastTrade;
		///Minute chart (without last ticker value)
		const std::vector<double> &minute_chart;
		///current position
		double position;
		///balance (currency)
		double balance;
		///current open price
		double open_price;
		///current leverage
		double cur_leverage;
		///is set true, when current current state is beyond max leverage
		bool max_leverage;
	};

	class ICache {
	public:
		virtual ~ICache() {}
	};

	///Summary of the strategy state
	/**
	 * This state is stored within MTrader, initially undefined, and strategy must
	 * define it first by calling onCycle. If the state is not initialized
	 * the trading can be is stopped
	 */
	struct StateSummary {
		///equilibrium
		double equilibrium = 0;
		///center price - spread will generate orders from this price
		double center_price = 0;
		///allocated budget (price * position + balance)
		double budget = 0;
		///position recorder by the strategy
		double position = 0;
		///total accumulated assets (automatically decreases position)
		double accumulated = 0;
		///neutral price (=0 disabled)
		double neutral_price = 0;
		///Cache object
		/**
		 * For the strategy convenience, any arbitrary data can be stored with the summary,
		 * and becomes available on next cycle, so strategy can use it as cache. The
		 * cache is cleared on trader's restart and after a trade
		 */
		std::unique_ptr<ICache> cache;
	};



	struct OrderReq {
		///-1 expects sell order, 1 expects buy order
		int dir;
		///suggested price of the order
		double order_price;
		///current price (bid or ask price)
		double current_price;
		///attempt - function getNewOrder can be called multiple times, this counts each attempt
		int attempt;
	};

	enum OrderResponseType {
		std_order, ///< standard order - can be converted to alert if cannot be fulfilled
		disable,  ///< order is disabled - no order will be generated
		alert, ///< generates alert - if the size is filled, then alert is generated when order cannot be fullfiled
		reject, ///< reject attempt, function can suggest optimal price
		market, ///< require market order (currently, not supported, it places order at top of the orderbook)
	};

	struct OrderData {
		///Meaning of this structure
		OrderResponseType rtype;
		///size size of the order -1 sell, +1 buy, 0 for alert
		double size ;
		///order price. Depend on rtype
		/**  \li \b std_order - place the order at specified price
		 *   \li \b disabled - ignored
		 *   \li \b alert -  place the alert at specified price
		 *   \li \b reject - suggest a price used for next attempt.
		 *   \li \b market - ignored
		 */
		double price = 0;
	};

	class IReport {
	public:
		///report a single number
		virtual void number(const std::string_view &title, double v) = 0;
		///report a string
		virtual void string(const std::string_view &title, const std::string_view &v) = 0;
		///report a boolean value
		virtual void boolean(const std::string_view &title, bool v) = 0;
		///report a position - function shows value in respect to the market type (inverted markets)
		virtual void position(const std::string_view &title, double position) = 0;
		///report a price - function shows value in respect to the market type (inverted markets)
		virtual void price(const std::string_view &title, double price) = 0;
	};

	struct MinMax {
		double min;
		double max;
	};

	struct CurvePoint {
		///point is defined, If false, other two fields are ignored
		bool valid;
		///position at given price
		double position;
		///calculated budget at given price
		double budget;
	};


	///Called for every cycle
	/**
	 * @param mst market state
	 * @param summary function must/should fill the summary. If the variable isn't initialized, the
	 * strategy must initialize the object and fill the values, otherwise, the trading is stopped.
	 * If the variable is initialized, the strategy can update the values. During idle
	 * situation, the variable keeps values from the previous cycle. The variable is reset
	 * when trader is started/restarted or immediately after any trade
	 *
	 * @return if strategy changed its state, function returns new instance.
	 *           Otherwise it can return self
	 */
	virtual PStrategy2 onCycle(const MarketState &mst, std::optional<StateSummary> &summary) const = 0;
	///Called to generate order
	/**
	 * @param mst market state
	 * @param req order request
	 * @return OrderData struct
	 */
	virtual OrderData getOrder(const MarketState &mst, const OrderReq &req) const = 0;
	///Called on trade
	/**
	 * @param mst market state
	 * @param trade information about the trade (eff_price and eff_size are values useful for calculation
	 * howver strategy can also use price and size to calculate effective fees
	 */
	virtual PStrategy2 onTrade(const MarketState &mst, const IStockApi::Trade &trade) = 0;
	///Called when strategy is not valid (before onInit)
	/**
	 * @param mst market  state
	 */
	virtual PStrategy2 onInit(const MarketState &mst) const = 0;
	///Generate report about strategy's state
	/**
	 * @param rpt report object. Function must feed rpt object with report
	 */
	virtual void report(IReport &rpt) const = 0;
	///Save state to json object
	virtual json::Value saveState() const = 0;
	///load state
	virtual PStrategy2 loadState(json::Value state) const = 0;
	///calculate safe tradeable range
	/**
	 * @param mst market state
	 * @param actual_assets actual assets on the market (can be different than position, especially when
	 * external assets are used)
	 * @param actual_currencies actual currencies on the market (can be different than position, especially
	 * when external currency are used)
	 * @return min and max price
	 */
	virtual MinMax calcSafeRange(const MarketState &ms,double actual_assets,double actual_currencies) const = 0;
	///Retrieves current's strategy ID
	virtual std::string_view getID() const = 0;
	///Calculates initial position
	/**
	 * Initial position should be calculated depend on configuration and market state. Strategy's state
	 * should not be involved in calculation (so function must return even if strategy is not valid)
	 */
	virtual double calcInitialPosition(const MarketState &mst) const = 0;
	///Calculate point on budget curve
	/**
	 * @param mst market state
	 * @param price for which price to calculate
	 * @return If strategy is not defined at given price, function should neturn invalid point.
	 * Otherwise the strategy returns budget and position at given price
	 *
	 * @note function is not used only for drawing a curve. It is also used to calculate various
	 * important values, for example currency allocation. For the leveraged markets, the function
	 * is called with current price and returned budget allocated. For spot markets, the function
	 * is called with price of the last trade, then currency is calculated (as budget - position*last_price)
	 * and the result is allocated
	 *
	 */
	virtual CurvePoint calcCurve(const MarketState &mst, double price) const = 0;



};



#endif /* SRC_MAIN_ABSTRACTSTRATEGY_H_ */
