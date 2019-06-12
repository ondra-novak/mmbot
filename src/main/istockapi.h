/*
 * istockapi.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTOCKAPI_H_
#define SRC_MAIN_ISTOCKAPI_H_

#include <imtjson/value.h>
#include "../shared/linear_map.h"
#include <string_view>

#include <imtjson/namedEnum.h>

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

	    static Trade fromJSON(json::Value v);
	    json::Value toJSON() const;
	};

	///Contains all trades - ordered by time
	using TradeHistory = std::vector<Trade>;


	struct TradeWithBalance: public Trade {
	public:
		double balance;
		static constexpr double no_balance = -9e99;

		TradeWithBalance() {}
		TradeWithBalance(const Trade &t, double balance):Trade(t),balance(balance) {}

	    static TradeWithBalance fromJSON(json::Value v);
	    json::Value toJSON() const;
};

	using TWBHistory = std::vector<TradeWithBalance>;


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
		std::uintptr_t time;
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

		template<typename Fn>
		double adjValue(double value, double step, Fn &&fn) const {
			return fn(value/step) * step;
		}
	};


	using Orders = std::vector<Order>;

	///Retrieves available balance for the symbol
	/**
	 * @param symb currency or asset symbol
	 * @return available balance
	 */
	virtual double getBalance(const std::string_view & symb) = 0;
	///Retrieves trades
	/**
	 * @param lastId last seen trade. Set to json::undefined if you has no trades
	 * @param fromTime specify timestamp (in milliseconds) of oldest trade to fetch
	 * @param pair specify trading pair
	 * @return list of trades
	 */
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) = 0;
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
	 *
	 * @param pair trading pair
	 * @param order order information
	 * @return order ID (can be string or number)
	 *
	 * @note To place new order, set it's id to json::undefined. If you specify
	 * a valid id, the specified order will be canceled and replaced by the new order
	 */
	virtual json::Value placeOrder(const std::string_view & pair, const Order &order) = 0;
	///Reset the API
	/**
	 * @retval true continue in trading
	 * @retval false stop trading
	 *
	 * @note external brokers cannot send 'false' as result. This is reserved for
	 * internal brokers and emulators.
	 */
	virtual bool reset() = 0;
	///Determines whether the API is emulator
	/**
	 * @retval true API is emulator
	 * @retval false API is not emulator
	 *
	 * @note external brokers cannot set this to 'true'
	 */
	virtual bool isTest() const = 0;

	///Retrieve market information
	/**
	 * @param pair trading pair
	 * @return market information
	 */
	virtual MarketInfo getMarketInfo(const std::string_view & pair) = 0;
	///Retrieves trading fees
	/**
	 *
	 * @param pair trading pair
	 * @return MAKER fees (the MMBot doesn't generate TAKER's orders)
	 */
	virtual double getFees(const std::string_view &pair) = 0;

	///Retrieve all available pairs
	virtual std::vector<std::string> getAllPairs() = 0;


	class Exception: public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};


	static json::NamedEnum<IStockApi::FeeScheme> strFeeScheme;
};

class IStockSelector{
public:
	using EnumFn = std::function<void(std::string_view, IStockApi &)>;
	virtual IStockApi *getStock(const std::string_view &stockName) const = 0;
	virtual void forEachStock(EnumFn fn) const = 0;
};


#endif /* SRC_MAIN_ISTOCKAPI_H_ */
