/*
 * strategy_hedge.h
 *
 *  Created on: 28. 5. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_HEDGE_H_
#define SRC_MAIN_STRATEGY_HEDGE_H_

#include "istrategy.h"

class Strategy_Hedge: public IStrategy {
public:



	struct Config {

		///hedge by holding long
		/** close long when price drops below specified price */
		/** open long when price return above this price */
		bool h_long;
		///hedge by taking short
		/** close short when price raises above specified price */
		/** open short when price drops below this price */
		bool h_short;

		///specify percentage of drop
		double ptc_drop;
	};

	struct State {
		double position=0;
		double low=0;
		double high=0;
		int ffs = 0;
		double last_price=0;
	};

	Strategy_Hedge(const Config &cfg);
	Strategy_Hedge(const Config &cfg, State &&state);

	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const override;
	virtual std::pair<IStrategy::OnTradeResult, PStrategy > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const
					override;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const override;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const override;
	virtual bool isValid() const override;
	virtual json::Value exportState() const override;
	virtual std::string_view getID() const override;
	virtual double getCenterPrice(double lastPrice, double assets) const
			override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const override;
	virtual IStrategy::BudgetInfo getBudgetInfo() const override;
	virtual double getEquilibrium(double assets) const override;
	virtual double calcCurrencyAllocation(double price, bool leveraged) const override;
	virtual IStrategy::ChartPoint calcChart(double price) const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const override;
	virtual PStrategy reset() const override;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const override;

	static const std::string_view id;

protected:

	Config cfg;
	State state;

};




#endif /* SRC_MAIN_STRATEGY_HEDGE_H_ */
