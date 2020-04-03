/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#define SRC_MAIN_STRATEGY_HYPERBOLIC_H_
#include <chrono>

#include "istrategy.h"



class Strategy_Hyperbolic: public IStrategy {
public:

	struct Config {
		double power;
		double asym;
		double max_loss;
		double reduction;
		double external_balance;
	};


	struct State {
		double neutral_price = 0;
		double last_price =0;
		double position = 0;
		double bal = 0;
		double val = 0;
		double pos_offset = 0;
	};

	Strategy_Hyperbolic(const Config &cfg, State &&st);
	Strategy_Hyperbolic(const Config &cfg);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;

	static std::string_view id;

protected:
	Config cfg;
	State st;
	mutable std::optional<MinMax> rootsCache;

	struct PosCalcRes {
		bool limited;
		double pos;
	};

	static Strategy_Hyperbolic init(const Config &cfg, double price, double pos, double currency);
	PosCalcRes calcPosition(double price) const;

	MinMax calcRoots() const;
	double adjNeutral(double price, double value) const;

	double calcMaxLoss() const;
	double calcMult() const;

	///Calculate neutral price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param position current position
	 * @param curPrice current price
	 * @return neutral price for given values
	 */
	static double calcNeutral(double power, double asym, double position, double curPrice);

	static double calcPosValue(double power, double asym, double neutral, double curPrice);
	///Calculate roots of value function (for maximum loss)
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral position
	 * @param balance balance allocate to trade (max loss)
	 * @return two roots which specifies tradable range
	 */
	static MinMax calcRoots(double power, double asym, double neutral, double balance);

	///Calculate power multiplicator
	/**
	 * @param bal current balance
	 * @param price current price
	 * @param cfg configuration
	 * @return
	 */
	static double calcMult(double bal, const Config &cfg) ;
	///Calculate position for given price
	/**
	 * @param power power multiplier
	 * @param asym asymmetry (-1;1)
	 * @param neutral neutral price
	 * @param price requested price
	 * @return position at given price
	 */
	static double calcPosition(double power, double asym, double neutral, double price);
	///Calculate price, where position is zero
	/**
	 * @param neutral_price neutral price
	 * @param asym asymmetry
	 * @return price where position is zero
	 */
	static double calcPrice0(double neutral_price, double asym);

	static double calcValue0(double power, double asym, double neutral);

	static double calcNeutralFromPrice0(double price0, double asym);

	static double calcPriceFromPosition(double power, double asym, double neutral, double position);
	///Calculates price from given value
	/**
	 * Position value is calculated using calcPosValue(). This function is inversed version if the function calcPosValue().
	 *
	 * @param power Power multiplier
	 * @param asym asymmetry (-1,1)
	 * @param neutral neutral price
	 * @param value position value
	 * @param curPrice on which price the value was retrieved. It is used to specify which side of the chart will be used, because there
	 * are always two results.
	 *
	 * @return found price. If you need position, call calcPosition on price. If the value is above of maximum value, result is middle price. To
	 * retrieve that value, call calcValue0()
	 */
	static double calcPriceFromValue(double power, double asym, double neutral, double value, double curPrice);

	static double calcNeutralFromValue(double power, double asym, double neutral, double value, double curPrice);

	double calcNewNeutralFromProfit(double profit, double price) const;
};


#endif /* SRC_MAIN_STRATEGY_HYPERBOLIC_H_ */

