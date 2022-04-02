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

#include "istockapi.h"


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


///market state - constants
struct MarketState {
	///market info
	const IStockApi::MarketInfo *minfo;
	///all trades
	const AbstractArray<IStockApi::Trade> *trades;

	///Event causing this operation
	MarketEvent event;

	///Timestamp for the event.
	/**
	 * If multiple events happened, the timestamp can be same for all events
	 */
	std::uint64_t timestamp;

	///current position
	double position;
	///balance (currency)
	double balance;
	///equity
	/** Equity contains position*cur_price+balance, but it is only balance on leveraged market */
	double equity;
	///current open price
	double open_price;
	///current leverage
	double cur_leverage;
	///current price aggregated
	double cur_price;
	///lowest price where you can put sell order
	double lowest_sell_price;
	///highest price where you can put buy order
	double highest_buy_price;
	///optimal buy price - calculated by spread generator
	double opt_buy_price;
	///optimal sell price - calculated by spread generator
	double opt_sell_price;
	///current realized pnl
	double rpnl;
	///current unrealized pnl
	double upnl;
	///is set to true, when buy order was rejected by the stock market during previous cycle
	bool buy_rejected;
	///is set to true, when sell order was rejected by the stock market during previous cycle
	bool sell_rejected;
	///trade now is active. Strategy should place order at op_sell_price or op_buy_price
	bool trade_now;
	///price of last trade
	double last_trade_price;
	///size of the last trade
	double last_trade_size;
	///assets live on market - can be different than position - can be used to calculate tradeable range
	double live_assets;
	///currencies live on markat - can be different than position - can be used to calculate tradeable range
	double live_currencies;
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
	 */
	virtual void set_currency_allocation(double allocation) = 0;
	///Sets equity allocation
	/**
	 * Allocates equity. Useful for leveraged markets, however it is used also for
	 * some calculations for UI. If not set (at least once), currency_allocation + last_trade_price*position is used
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

class AbstractStrategyFactory {
public:
	virtual ~AbstractStrategyFactory() {}
	virtual PStrategy3 create(json::Value config) = 0;
	virtual std::string_view get_id() const = 0;
	virtual json::Value get_form_def() const = 0;
};

class AbstractStrategyRegister {
public:
	virtual ~AbstractStrategyRegister() {}
	virtual void reg_strategy(std::unique_ptr<AbstractStrategyFactory> &&factory) = 0;

	template<typename Fn>
	void reg(std::string_view id, Fn &&fn, json::Value form_def) {
		class Call: public AbstractStrategyFactory {
		public:
			Call(std::string_view id, Fn &&fn, json::Value form_def)
				:id(id),fn(std::forward<Fn>(fn)), form_def(form_def) {}
			PStrategy3 create(json::Value config) override {
				return fn(config);
			}
			virtual std::string_view get_id() const override {
				return id;
			}
			virtual json::Value get_form_def() const override {
				return form_def;
			}
		protected:
			std::string id;
			Fn fn;
			json::Value form_def;
		};
		reg_strategy(std::make_unique<Call>(id,std::forward<Fn>(fn), form_def));
	}

};



#endif /* SRC_MAIN_ISTRATEGY3_H_ */
