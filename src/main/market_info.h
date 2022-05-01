/*
 * market_info.h
 *
 *  Created on: 28. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_MARKET_INFO_H_
#define SRC_MAIN_MARKET_INFO_H_

#include <imtjson/value.h>
#include <imtjson/namedEnum.h>
#include "acb.h"


enum class FeeScheme {
	///Fees are substracted from the currency
	currency,
	///Fees are substracted from the asset
	assets,
	///Fees are substracted from income (buy - assets, sell - currency)
	income,
	///Fees are substracted from outcome (buy - currency, sell - assets)
	outcome

};

enum class MarketType {
	///normal market
	normal,
	///inverted market
	inverted
};

double calc_profit(MarketType tp, double pos, double enter, double exit);
double calc_spot_equity(MarketType tp, double balance, double pos, double price);

extern json::NamedEnum<FeeScheme> strFeeScheme;
extern json::NamedEnum<MarketType> strMarketType;

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
	FeeScheme feeScheme = FeeScheme::currency;
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

	///(Deprecated) The broker inverts price when currency is quoted instead assets
	/** Currently 'deribit' broker inverts price because the position is quoted
	 * in currency. This result, that price is returned as 1/x.
	 *
	 * By setting this field to true, all prices will be inverted back
	 * when they are put to the report. The broker should also report
	 * correct symbol of inverted price
	 */
	bool invert_price = false;

	///Symbol denotes the asset price, can be different for inverted market, but has to be always specified even if it equals currency_symbol
	std::string quoted_symbol;

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
	MarketType type = MarketType::normal;

	void addFees(double &assets, double &price) const;
	void removeFees(double &assets, double &price) const;

	double priceAddFees(double price, double side) const;
	double priceRemoveFees(double price, double side) const;

	///calculate equity
	/** for leveraged markets, equity is exact balance, assets and position are not used.
	 * For spot market it depends whether it is inverted or normal market
	 *
	 * @param assets current assets / position
	 * @param balance current currency balance
	 * @param price current price
	 * @return calculated equity
	 */
	double calcEquity(double assets, double balance, double price) const;
	///Calculates equity on price1, when last trade was executed on price0
	/**
	 * @param assets assets held after execution on price0
	 * @param balance balance after execution on price0
	 * @param price0 price of execution
	 * @param price1 current price
	 * @return equity on price1. Function determines market type (normal/inverted, spot/leveraged) and
	 * chooses right formula to calculate the equity
	 */
	double calcEquity(double assets, double balance, double price0, double price1) const;
	///Calculate profit between enter and exit price for given position
	/**
	 * @param position a position active from price_enter
	 * @param price_enter price at the position has been opened
	 * @param price_exit current price or exit price (position closed)
	 * @return profit or loss using correct formula depend on market type;
	 */
	double calcProfit(double position, double price_enter, double price_exit) const;

	///Calculate position value for given price
	/** position value is calculated as position * price for normal market and position / price as inverted
	 * market. Position value is always positive number even for shorts
	 *
	 * @param position position
	 * @param price price
	 * @return position value
	 */
	double calcPosValue(double position, double price) const;

	///Calculate position if you know position value
	/**
	 * @see calcPosValue
	 * @note Always returns positive value, you need to multiply by -1 if you know, that position is short
	 *
	 * @param value position value
	 * @param price price
	 *
	 * @return position
	 */
	double calcPosFromValue(double value, double price) const;

	///Calculates how much currency changed byt executing trade at price
	/**this also respects, whether trade is executed on leverage market, where currecy is not spend
	 *
	 * @param size spend size
	 * @param price price
	 * @param no_leverage set true to calculate even if leverage is active
	 * @return currency change
	 */
	double calcCurrencyChange(double size, double price, bool no_leverage = false) const;

	double sizeFromCurrencyChange(double change, double price) const;

	///Initialize ACB object depend on market type;
	ACB initACB (double open_price, double position, double realized_pnl = 0) const;
	///Initialize ACB object depend on market type (initialized to position 0);
	ACB initACB () const;

	///invert position if current market is inverted, no change if not
	double invert_pos_if_needed(double pos) const {
		if (type == MarketType::inverted) return -pos; else return pos;
	}
	///invert position if current market is inverted, no change if not
	double invert_price_if_needed(double price) const {
		if (type == MarketType::inverted) return 1.0/price; else return price;
	}

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



#endif /* SRC_MAIN_MARKET_INFO_H_ */
