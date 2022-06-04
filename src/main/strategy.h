/*
 * strategy.h
 *
 *  Created on: 17. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_H_
#define SRC_MAIN_STRATEGY_H_

#include "../shared/ini_config.h"
#include "istrategy.h"

class Strategy {
public:

	using Ptr = PStrategy;
	using TradeResult = IStrategy::OnTradeResult;
	using MinMax = IStrategy::MinMax;
	using OrderData = IStrategy::OrderData;

	Strategy(const Ptr ptr):ptr(ptr) {}
	///Returns true, if the strategi is valid and initialized
	/**
	 * @retval true valid
	 * @retval false not valid
	 */
	bool isValid() const {return ptr!=nullptr && ptr->isValid();}
	///Called on each cycle, after status of the market is read but when there were no trades created
	/**
	 * @param minfo market info
	 * @param curPrice current market ticker
	 * @param assets asset balance
	 * @param currency currency balance
	 */
	void onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) {
		ptr = ptr->onIdle(minfo, curTicker, assets, currency);
	}
	///Called on each cycle when trade has been created
	/**
	 * @param minfo markey info
	 * @param tradePrice price of trade (fee substracted)
	 * @param tradeSize size of the trade (fee substracted). If this field is 0, then
	 *   the trade is result of the function "accept loss". In this case, the strategy
	 *   should reset itself to unblock trading, because there is not enough assets
	 *   or currencies on the account
	 * @param assetsLeft remain assets on account
	 * @param currencyLeft remain currency on account
	 * @return The strategy must calculate normalized profit and normalized accumulated assets
	 */
	TradeResult onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
			double assetsLeft, double currencyLeft)  {
		auto t = ptr->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);
		ptr = t.second;
		return t.first;
	}

	///Exports internal state to JSON
	json::Value exportState() const;

	///Imports internal state from JSON
	/**
	 * @param src source json
	 * @param minfo market info - allows to correctly initialize calculated parts
	 */
	void importState(json::Value src, const IStockApi::MarketInfo &minfo);

	///Requests the strategy to calculate order
	/**
	 * Function called twice for buy and sell order.
	 *
	 * @param minfo market info
	 * @param cur_price current price (from ticker). It defines limit how the price can be changed
	 * @param new_price new price of the order
	 * @param assets remain assets on account
	 * @param currency remain currency on account
	 * @param dir specified expected direction. +1 = buy, -1 = sell. If the strategy generates
	 * oposite direction, the next behaviour depends on the option "dust orders". If this
	 * option is turned on, a dust order is issued instead atd given price. If the option is turned off no order is issued
	 * @param rej is set to true, if the function is called repeatedly, because prevous order has been rejected
	 * @return data used to create new order
	 */
	OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const {
		return ptr->getNewOrder(minfo, cur_price, new_price, dir, assets, currency, rej);
	}
	///Calculates safe range
	/**
	 * @param minfo market info
	 * @param assets remaining assets
	 * @param currency remaining currency
	 * @return min-max values. To express "infinity", use std::numeric_limits::infinity()
	 */
	MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currency) const {
		return ptr->calcSafeRange(minfo, assets, currency);
	}

	///Returns equilibrium
	double getEquilibrium(double assets) const {
		return ptr->getEquilibrium(assets);
	}

	///Resets strategy - remove any remembered internal state
	void reset() {
		ptr = ptr->reset();
	}

	///dumps state of strategy in pretty format (best to display on admin page)
	json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const  {
		return ptr->dumpStatePretty(minfo);
	}

	auto getID() const {
		return ptr->getID();
	}

	double calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
		return ptr->calcInitialPosition(minfo, price, assets, currency);
	}

	IStrategy::BudgetInfo getBudgetInfo() const {
		return ptr->getBudgetInfo();
	}

	IStrategy::ChartPoint calcChart(double price) const {
		return ptr->calcChart(price);
	}

	double getCenterPrice(double lastPrice, double assets) const {
		return ptr->getCenterPrice(lastPrice,assets);
	}


	///Calculates how much currency is allocated for strategy
	/** Function is used to allocate budget from currency pool
	 * This helps to share budget between traders
	 * @param price specify current price.
	 * @return amount of currency to allocate, or 0 if the operation is not meaningfull - for leverage markes for example
	 */
	double calcCurrencyAllocation(double price, bool leveraged) const {
		return ptr->calcCurrencyAllocation(price, leveraged);
	}

	static Strategy create_base(std::string_view id, json::Value config);
	static Strategy create(std::string_view id, json::Value config);

	Strategy invert() const;



protected:
	Ptr ptr;



};


#endif /* SRC_MAIN_STRATEGY_H_ */
