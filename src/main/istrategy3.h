/*
 * istrategy3.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTRATEGY3_H_
#define SRC_MAIN_ISTRATEGY3_H_
#include <memory>
#include <shared/refcnt.h>

#include "abstractarray.h"
#include "tool_register.h"

#include "istockapi.h"


namespace json {
	template<typename T> class NamedEnum;
}

class IStrategy3;
class AbstractTraderControl;

using PStrategy3 = ondra_shared::RefCntPtr<const IStrategy3>;

enum class MarketEvent {
	///trader has started, strategy should initialize internals by its state
	start,
	///idle cycle, nothing has happened, just price has changed
	idle,
	///trade was detected
	trade,
	///alert was detected
	alert
};


extern json::NamedEnum<MarketEvent> strMarketEvent;

using AbstractTradeList = AbstractArray<IStockApi::Trade>;

///market state - constants
struct MarketState {
	///market info
	const IStockApi::MarketInfo *minfo;
	///all trades
	const AbstractTradeList *trades;
	///Event causing this operation
	MarketEvent event; //start, idle, trade
	///Current time - time when the strategy is called
	std::uint64_t cur_time;
	///Event time - time when reported event happened
	std::uint64_t event_time;
	///current position
	double position;
	///balance (currency)
	double balance;
	///equity - position*cur_price+balance, but it is only balance on leveraged market */
	double equity;
	///current open price
	double open_price;
	///current leverage
	double cur_leverage;
	///current price aggregated
	double cur_price;
	///price for to current event
	double event_price;
	///equity calculated for the event
	double event_equity;
	///lowest price where you can put sell order
	double lowest_sell_price;
	///highest price where you can put buy order
	double highest_buy_price;
	///optimal buy price - calculated by spread generator
	double sug_buy_price;
	///optimal sell price - calculated by spread generator
	double sug_sell_price;
	///price of last trade
	double last_trade_price;
	///size of the last trade
	double last_trade_size;
	///assets live on market - can be different than position - can be used to calculate safe range
	double live_assets;
	///currencies live on market - can be different than position - can be used to calculate safe range
	double live_currencies;
	///current realized pnl
	double rpnl;
	///current unrealized pnl
	double upnl;

	///is set to true, when buy order was rejected by the stock market during previous cycle
	bool buy_rejected;
	///is set to true, when sell order was rejected by the stock market during previous cycle
	bool sell_rejected;
	///trade now is active. Strategy should place order to be executed as soon as possible
	/// By default sug_buy_price and sug_sell_price are set accordingly
	bool trade_now;
	///All values has been inverted for the strategy because inverted market
	/** The strategy can calculate same way as for normal market, however, minfo refers to original market
	 * not inverted version.
	 */
	bool inverted;
};

enum class OrderRequestResult {
	///order is accepted and will be placed
	accepted,
	///order was partially accepted - not full size can be fulfilled. You can find final size in additional info
	partially_accepted,
	///order size is invalid, size must be positive, no additional info (only for buy() or sell())
	invalid_size,
	///order price is invalid, no additional info
	invalid_price,
	///order was too small, additional info is set to minsize
	too_small,
	///order cannot be placed, because max leverage is reached, no additional info
	max_leverage,
	///
	no_funds,

	max_position,

	min_position,

	max_costs
};



struct NewOrderResult {
	///state of operation
	OrderRequestResult state;
	///additional information
	double v;

	bool isOk() const {return state == OrderRequestResult::accepted||state == OrderRequestResult::partially_accepted;};
};



struct MinMax {
	double min;
	double max;
};



class AbstractTraderControl{
public:
	///Retrieve current market state
	virtual const MarketState &get_state() const = 0;


	///Change position at specified price, different way to buy or sell
	/**
	 * @param new_pos specifies new position.
	 * @param price price where the position will be changed. Note that you can increase position only at
	 * lower price or decrease position on higher price. If you se this on 0 (default), function will use optimal
	 * price
	 * @retval accepted order accepted
	 * @retval partially accepted order accepted partially. Additional info contains final position after execution
	 * @retval invalid_price can't place order at specified price
	 * @retval too_small result order was too small. Additional info contains suggested position.
	 * @retval max_leverage place failed because max leverage was reached
	 * @retval user_limited place failed because user limited position somehow (min or max position, max cost etc)
	 */
	virtual NewOrderResult alter_position(double new_pos, double price = 0) = 0;


	///Place buy order
	/**
	 * @param price order price. It can be set to 0, then opt_buy_price will be used
	 * @param size size of the order. It can be set to 0, then alert will be placed
	 * @retval result of the operation
	 *
	 * @note You cannot place multiple orders. Any additional request replaces previous request.
	 *
	 * @note No other is placed or canceled without calling this function.
	 */
	virtual NewOrderResult  limit_buy(double price, double size) = 0;
	///Place sell order
	/**
	 * @param price order price. It can be set to 0, then opt_sell_price will be used
	 * @param size size of the order. It can be set to 0, then alert will be placed
	 * @retval result of the operation
	 *
	 * @note You cannot place multiple orders. Any additional request replaces previous request.
	 *
	 * @note No order is placed or canceled without calling this function.
	 */
	virtual NewOrderResult  limit_sell(double price, double size) = 0;

	///Place market buy order
	/**
	 * You can only have one pending market order. Another call to this method
	 * replaces an already pending order (the order is scheduled and executed as
	 * soon as the strategy returns control to the Trader)
	 * @param size size of order
	 * @return validation status
	 */
	virtual NewOrderResult  market_buy(double size) = 0;

	///Place market buy order
	/**
	 * You can only have one pending market order. Another call to this method
	 * replaces an already pending order (the order is scheduled and executed as
	 * soon as the strategy returns control to the Trader)
	 * @param size size of order
	 * @return validation status
	 */
	virtual NewOrderResult  market_sell(double size) = 0;
	///Clear the buy order
	/** If there is no buy order, function does nothing. You don't need to call this function
	 * if you plan to immediately place a buy order as there cannot be multiple buy orders
	 */


	virtual void cancel_buy() = 0;
	///Clear the sell order
	/** If there is no sell order, function does nothing. You don't need to call this function
	 * if you plan to immediately place a sell order as there cannot be multiple buy orders
	 */
	virtual void cancel_sell() = 0;
	///Sets new equilibrium price
	/**
	 * @param price new equilibrium price
	 * @note Before the  `start` event and before the `trade` event, the equilibrium price is
	 * reset to last trade price. However the strategy should calculate correct
	 * equilibrium price during at least these two events
	 */
	virtual void set_equilibrium_price(double price) = 0;
	///Sets new safe range
	/**
	 * @param minmax safe range
	 * @note the strategy should use live_assets and live_currencies to calculate safe range.
	 * This should be done at least during `start` or `trade` event. Calculating often
	 * can cause impact to performance
	 */
	virtual void set_safe_range(const MinMax &minmax) = 0;
	///Sets currency allocation
	/**
	 * @param allocation allocation of currency
	 *
	 * @note The strategy should call this function during `start` event and should
	 * update allocation appropriately as situation changes on market. On spot markets, this
	 * can be done during `trade` event
	 *
	 * @note It is recommended to use set_equity_allocation
	 */
	virtual void set_currency_allocation(double allocation) = 0;
	///Sets equity allocation
	/**
	 * Allocates equity. Equity consists of currency and value of held assets.
	 * To measure normalized profit, the trader will compare reported allocation
	 * between each trade by following formula
	 *
	 * equity_current_trade - equity_alloc_current_trade - equity_prev_trade +equity_alloc_prev_trade
	 *
	 * It is possible to calculate equity allocation when MarketEvent::idle using the current
	 * price, however you should use last_trade_price to calculate equity allocation when MarketEvent::trade
	 *
	 * @param allocation current equity allocation
	 *
	 * This function also sets currency allocation simply by substracting position
	 * value from the equity and result value is used to calculate extra balance
	 *
	 * @note function should be used instead set_currency_allocation()equity_prev_trade
	 *
	 */
	virtual void set_equity_allocation(double allocation) = 0;

	///Report neutral price (for UI)
	virtual void report_neutral_price(double neutral_price) = 0;
	///Report arbitrary price
	/**
	 * Reports any arbitrary price. You should use this instead report_number because
	 * it can correctly process the value, if there are some conversion (for example inverted markets)
	 * @param title title
	 * @param value value
	 */
	virtual void report_price(std::string_view title, double value) = 0;
	///Report arbitrary position
	/**
	 * Reports any arbitrary position. You should use this instead report_number because
	 * it can correctly process the value, if there are some conversion (for example inverted markets)
	 * @param title title
	 * @param value value
	 */
	virtual void report_position(std::string_view title, double value) = 0;
	///Reports percents
	/**
	 * @param title title
	 * @param value value
	 *
	 * @note result is x100, so 0.5 is reported as 50%
	 */
	virtual void report_percent(std::string_view title, double value) = 0;

	///Reports percents
	/**
	 * @param title title
	 * @param value value
	 * @param base base value
	 *
	 * @note reports value/base x100 in percents
	 */
	virtual void report_percent(std::string_view title, double value, double base) = 0;


	///Report arbitrary number
	virtual void report_number(std::string_view title, double value) = 0;
	///Report arbitrary string
	virtual void report_string(std::string_view title, std::string_view string) = 0;
	///Report arbitrary boolean
	virtual void report_bool(std::string_view title, bool value) = 0;
	///Removes item identified by the tittle
	virtual void report_nothing(std::string_view title) = 0;
	virtual ~AbstractTraderControl() {}

	///Sets buy order error
	/** Allows to show error message with the order
	 *
	 * @param text displayed text
	 * @param display_price displayed price. If set to 0, ---- is used instead price
	 * @param display_size displayed size. The price must be set to display size
	 *
	 * @note automatically cancels buy order
	 */
	virtual void set_buy_order_error(std::string_view text, double display_price = 0, double display_size = 0) = 0;
	///Sets sell order error
	/** Allows to show error message with the order
	 *
	 * @param text displayed text
	 * @param display_price displayed price. If set to 0, ---- is used instead price
	 * @param display_size displayed size. The price must be set to display size
	 *
	 * @note automatically cancels sell order
	 */
	virtual void set_sell_order_error(std::string_view text, double display_price = 0, double display_size = 0) = 0;

	///Write anything to trade's log file
	/** Each call creates one line. It is logged along with timestamp
	 *
	 * @param text
	 */
	virtual void log(std::string_view text) = 0;

	///Calculate minimal order size at given price
	/** More accurate funtion than minfo.getMinSize() because it respects inverted markets */
	virtual double calc_min_size(double price) = 0;

};

struct ChartPoint {
	bool valid;
	double position;
	double equity;
};


class IStrategy3: public ondra_shared::RefCntObj {
public:
	virtual ~IStrategy3() {}
	virtual PStrategy3 run(AbstractTraderControl &cntr) const = 0;
	virtual PStrategy3 load(const json::Value &state) const = 0;
	virtual PStrategy3 reset() const = 0;
	virtual json::Value save() const = 0;
	virtual ChartPoint get_chart_point(double price) const = 0;
	virtual double calc_initial_position(const MarketState &st) const = 0;
	virtual std::string_view get_id() const = 0;
};


using AbstractStrategyRegister = AbstractToolRegister<PStrategy3>;
using AbstractStrategyFactory = AbstractToolFactory<PStrategy3>;
using StrategyRegister = ToolRegister<PStrategy3>;
template<> class ToolName<PStrategy3> {
public:
	static std::string_view get();
};


#endif /* SRC_MAIN_ISTRATEGY3_H_ */
