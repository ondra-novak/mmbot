/*
 * istockapi.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTOCKAPI_H_
#define SRC_MAIN_ISTOCKAPI_H_

#include <memory>
#include <chrono>
#include <imtjson/value.h>
#include <imtjson/namedEnum.h>
#include <userver/callback.h>

///Interface definition for accessing a stockmarket
/** Contains minimal set of operations need to be implemented to access the stockmarket */
class IStockApi {
public:


	///Definition of trade
	struct Trade {
		///Trade identifier - It used to specify last seen trade while syncing
		json::Value id;
		///Time of creation (in milliseconds)
		std::uint64_t time;
		///Amount of assets has been traded
		/** The value is NEGATIVE for sell trade or POSITIVE for buy trade */
	    double size;
	    ///price for one item
	    double price;
	    ///effective size after applying fees
	    double eff_size;
	    ///effective price after applying fees
	    double eff_price;
	    ///OrderId - if available
	    json::Value order_id;

	    static Trade fromJSON(json::Value v);
	    json::Value toJSON() const;
	};

	///Contains all trades - ordered by time
	using TradeHistory = std::vector<Trade>;


	struct TradeWithBalance: public Trade {
	public:
		//total balance (including external assets) after the trade
		double balance;
		//true if the trade was detected as manual trade
		bool manual_trade;
		static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();



		TradeWithBalance() {}
		TradeWithBalance(const Trade &t,
				double balance,
				bool manual):Trade(t)
					,balance(balance)
					,manual_trade(manual) {}

	    static TradeWithBalance fromJSON(json::Value v);
	    json::Value toJSON() const;
};


	///Order
	struct Order {
		//Order id - undefined when placed new, otherwise contains order to replace
		json::Value id;
		//client id
		json::Value client_id;
		//size, negative sell, positive long
		double size;
		//price
		double price;

		static Order fromJSON(json::Value v);
		json::Value toJSON() const;

	};

	///Current asset value
	struct Ticker {
		///The first bod
		double bid;
		///The first ask
		double ask;
		///Last price
		double last;
		///Time when read
		std::uint64_t time;
	};

	enum FeeScheme {
		///Fees are substracted from the currency
		currency,
		///Fees are substracted from the asset
		assets,
		///Fees are substracted from income (buy - assets, sell - currency)
		income,
		///Fees are substracted from outcome (buy - currency, sell - assets)
		outcome
	};

	///Information about market
	struct MarketInfo {
		///Symbol of asset (must match with symbol in balancies)
		std::string asset_symbol;
		///Symbol of currency (must match with symbol in balancies)
		std::string currency_symbol;
		///Smallest change of amount
		double asset_step;
		///Smallest change of currency
		double currency_step;
		///Smallest allowed amount
		double min_size;
		///Smallest allowed of volume (price*size)
		double min_volume;
		///Fees of volume 1 (0.12% => 0.0012)
		double fees;
		///How fees are handled
		FeeScheme feeScheme = currency;
		///Leverage if margin trading is involved
		/** Default value is 0, which means no margin is available
		 * When this field is set, this changes several calculations
		 * in report. It doesn't affect trading. By setting leverage also
		 * enables short trades. However you still need to set external_assets
		 * to specify starting point
		 *
		 * @note when leverage is set, command 'achieve' expects position,
		 * not total assets
		 */
		double leverage = 0;

		///The broker inverts price when currency is quoted instead assets
		/** Currently 'deribit' broker inverts price because the position is quoted
		 * in currency. This result, that price is returned as 1/x.
		 *
		 * By setting this field to true, all prices will be inverted back
		 * when they are put to the report. The broker should also report
		 * correct symbol of inverted price
		 */
		bool invert_price = false;

		///When invert_price is true, the broker should also supply symbol name of inverted price
		std::string inverted_symbol;

		///This flag must be true, if the broker is just simulator and doesn't do live trading
		/** Simulators are not included into daily performance */
		bool simulator = false;

		///Set this flag to disable of sharing chart data
		/** Default settings is not shared, however if storage_broker is used, the chart data can
		 * be shared with other users. This flag is copied into trader_state as "private_chat", which can
		 * be read by the storage_broker to store chart data which prevents sharing
		 */
		bool private_chart = false;

		///Specifies wallet identifier for this pair
		/**This allows to broker to expose how balance is shared between traders.
		 * Each pair can use different wallet, so their balances are not shared. If
		 * the symbols are from the same wallet, the balance is shared between traders
		 * and each trader can allocate part of balance. Default value is "", which is
		 * also identified as single wallet
		 */
		std::string wallet_id;
		///Adds fees to values
		/**
		 * @param assets reference to current asset change. Negative value is sell,
		 * positive is buy.
		 * @param price reference to trade price
		 *
		 * Function updates value to reflect current fee scheme. If the fee scheme
		 * substracts from the currency, the price is adjusted. If the fee scheme
		 * substracts from the assets, the size is adjusted
		 */
		void addFees(double &assets, double &price) const;
		void removeFees(double &assets, double &price) const;

		double priceAddFees(double price, double side) const;
		double priceRemoveFees(double price, double side) const;

		template<typename Fn>
		static double adjValue(double value, double step, Fn &&fn)  {
			if (step == 0) return value;
			return fn(value/step) * step;
		}
		json::Value toJSON() const;
		static MarketInfo fromJSON(const json::Value &v);
		std::int64_t priceToTick(double price) const;
		double tickToPrice(std::int64_t tick) const;
		double getMinSize(double price) const;
	};


	struct TradesSync {
		TradeHistory trades;
		json::Value lastId;
	};

	using Orders = std::vector<Order>;

	///Retrieves available balance for the symbol
	/**
	 * @param symb currency or asset symbol
	 * @param pair pair information - helps to broker idenitify which value to return in case to separated balances
	 * @return available balance
	 */
	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) = 0;
	///Retrieves trades
	/**
	 * @param lastId last seen trade. Set to json::undefined if you has no trades
	 * @param fromTime specify timestamp (in milliseconds) of oldest trade to fetch
	 * @param pair specify trading pair
	 * @return list of trades
	 */
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) = 0;

	///Retrieve open orders
	/**
	 * @param par trading pair
	 * @return all opened orders
	 */
	virtual Orders getOpenOrders(const std::string_view & par) = 0;
	///Retrieve asset's current price
	/**
	 * @param piar trading pair
	 * @return ticker information
	 */
	virtual Ticker getTicker(const std::string_view & piar) = 0;
	///Place new order
	/**
	 * @param pair pair identifier
	 * @param size size of the order. Positive number is buy, negative number is sell,
	 *   zero causes, that new order will not be placed (however, can be used to cancel
	 *   other order)
	 * @param price price of the LIMIT order. Set zero to place MARKET order
	 * @param clientId client's identifier associated with the order.
	 * @param replaceId (optional) if specified, function cancels specified order before
	 * it places new one
	 * @param replaceSize (optional) check, whether the replacing order has remaining
	 * size equal or above specified size. If the replacing order has size below this
	 * number, the new order is not placed. This operation can cause, that replacing
	 * order will be canceled because this can be emulated by cancel+check+place. If
	 * the check fails, the old order is canceled, but new order is not placed.
	 * NOTE: The absoulute value of the number is taken and compared
	 *
	 * @return Function returns ID of new order. If the order was not placed but
	 * retains old order, the function returns replaceId. If the order was
	 * canceled but new order was not placed, the function returns null.
	 *
	 * @exception any if the function throws an exception, the state of the market
	 * should be unchanged. However, most APIs doesn't support transactions, so
	 * failure of placing order can result to canceling previous order but not placing
	 * the new one.
	 */
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId = json::Value(),
			json::Value replaceId = json::Value(),
			double replaceSize = 0) = 0;



	struct NewOrder {
		///Symbol or pair
		/** Note this field is just string_view. Ensure, that contains a valid content before
		 * the function is called
		 */
		std::string_view symbol;
		///Order size and direction.
		/** A positive value is buy, a negative value is sell. If this field is set to zero, no
		 * order is placed
		 */
		double size;
		///Price where the order place
		/**
		 * If zero is set a MARKET order is placed, otherwise a LIMIT order is placed at specified price.
		 * Note that LIMIT orders are post-only to keep fees lowest
		 **/
		double price;
		///Allows to mark order with arbitrary client_id. However it is recommended to use uint32. The
		/// value don't need to be unique
		json::Value client_id;
		/// Specifies ID of order to be replaced or edited.
		/**If the exachange supports order editing, the
		 * order can be edited, which is ofter much faster then cancel+place. The implementation chooses
		 * optimal way how to replace a specified order (note: use exchange's id, not clinet_id)
		 */
		json::Value replace_order_id;
		/// Specifies size condition when editing is allowed
		/** Expected size of the replacing order to edit or place new order. This helps against duplicate
		 * executions especially when the first execution is partially only. The feature refuses to
		 * place or edit the order, if the size of the replacing order is below the specified size. It can
		 * mean, that there is unprocessed execution, so the new order cannot be placed. The most
		 * exchanges reports the final size of the order after cancelation. If such order has remaining
		 * size below specified constrain, the new order is not placed and it is expected, that
		 * execution will be processed during the next cycle. The value should be always positive, regardless
		 * on, whether it was buy or sell. To cancel order uncoditionally, set this field to zero
		 */
		double replace_excepted_size;
	};

	using OrderList = std::vector<NewOrder>;
	using OrderIdList = std::vector<json::Value>;
	using OrderPlaceErrorList = std::vector<std::string>;


	///Place multiple orders
	/**
	 * @param pair selected pair - you can place multiple orders for single pair (symbol)
	 * @param orders list of orders to place
	 * @param ids when function returns, contains list of IDS of new orders. When null is returned,
	 * order was not placed
	 * @param errors for each order, an error can apper. If no error reported, the apropriate string
	 * is empty
	 *
	 * @note for every order there is one item in ids and one item in errors. Only if an item within
	 * 'errors' is non-empty string means there were an error. It can happen, that null + empty string
	 * is returned which means, that order was not placed, but no error was generated. This can
	 * also happen, when order is canceled without placing a new order
	 */
	virtual void batchPlaceOrder(const OrderList &orders, OrderIdList &ret_ids, OrderPlaceErrorList &ret_errors) = 0;

	///Reset the API
	/**
	 * @param tp time point for which reset is called. Multiple calls with the same timepoint
	 * helps to detect additional unecessery calls (for example to reset broker which simulates
	 * other broker)
	 *
	 */
	virtual void reset(const std::chrono::system_clock::time_point &tp) = 0;

	///Retrieve market information
	/**
	 * @param pair trading pair
	 * @return market information
	 */
	virtual MarketInfo getMarketInfo(const std::string_view & pair) = 0;
	//Retrieves trading fees
	/*
	 *
	 * @param pair trading pair
	 * @return MAKER fees (the MMBot doesn't generate TAKER's orders)
	 *
	 * NOTE: Removed from API, fees are updated throught getMarketInfo - called after every trade
	 */
	//virtual double getFees(const std::string_view &pair) = 0;

	virtual ~IStockApi() {}

	static json::NamedEnum<IStockApi::FeeScheme> strFeeScheme;

};

using PStockApi = std::shared_ptr<IStockApi>;


#endif /* SRC_MAIN_ISTOCKAPI_H_ */
