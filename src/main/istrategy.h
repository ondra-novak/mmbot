/*
 * istrategy.h
 *
 *  Created on: 17. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISTRATEGY_H_
#define SRC_MAIN_ISTRATEGY_H_
#include <string_view>
#include <imtjson/value.h>
#include "../shared/refcnt.h"
#include "istockapi.h"

class IStrategy;
using PStrategy = ondra_shared::RefCntPtr<const IStrategy>;

class IStrategy: public ondra_shared::RefCntObj {
public:

	enum class Alert {
		//alerts are disabled. If the order cannot be placed, the MTrader will try different price - when order fails to place, order doesn't appear
		disabled,
		//alerts are enabled however it appearence is controlled by the MTrader. It still can cause that MTrader will try different price. When order fails to place, alert is placed instead
		enabled,
		//alert is enforced. Order or alert is placed at given price without searching for better price
		forced,
		//order is stoploss - no futher modification are allowed. If the order cannot be executed, it is converted to alert
		stoploss
	};

	struct OnTradeResult {
		///normalized profit
		double normProfit;
		///normalized accumulated
		double normAccum;
		///neutral position, if 0, the value is not drawn on chart
		double neutralPrice = 0;
		///open price, if 0, the value is not drawn
		double openPrice = 0;
	};

	struct OrderData {
		///price where order is put. If this field is 0, recommended price is used
		double price;
		///size of the order, +buy, -sell. If this field is 0, the order is not placed
		double size;
		///set true, to put alert/dust order. This needs size equal to zero
		Alert alert = Alert::enabled;
	};



	struct MinMax {
		double min;
		double max;
	};

	struct BudgetInfo {
		//total budget in currency
		double total;
		//assets
		double assets;
	};

	struct BudgetExtraInfo {
		double total;
		double extra;
	};

	struct ChartPoint {
		bool valid;
		double position;
		double budget;
	};

	virtual bool isValid() const = 0;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const = 0;
	virtual std::pair<OnTradeResult, PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const = 0;
	virtual json::Value exportState() const = 0;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const = 0;
	virtual PStrategy importState(json::Value src, const IStockApi::MarketInfo &minfo) const = 0;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const = 0;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const = 0;
	virtual double getEquilibrium(double assets) const = 0;
	virtual PStrategy reset() const = 0;
	virtual std::string_view getID() const = 0;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const = 0;
	virtual BudgetInfo getBudgetInfo() const = 0;
	virtual double calcCurrencyAllocation(double price) const = 0;
	virtual ~IStrategy() {}
	virtual ChartPoint calcChart(double price) const = 0;
	///whether strategy need live balance onIdle()
	/**
	 * @retval false balance is updated only after trade
	 * @retval true balance is update on each cycle
	 *
	 * If strategy doesn't need live balance, it can make whole cycle faster, because queries to balance are skipped
	 */
	virtual bool needLiveBalance() const = 0;


protected:
	///Calculates order size
	/**
	 *
	 * @param expectedAmount expected amount of assets on the exchange
	 * @param actualAmmount actual amount of the assets on the exchange (can be different)
	 * @param newAmount new amount to achieve on th exchange
	 * @return size of the order
	 *
	 * @note the simple formula is to substract newAmount from actualAmount. However the
	 * formula is slightly relaxed to avoid large orders if the actualAmount is much different
	 * than expectedAmount.
	 *
	 */
	static double calcOrderSize(double expectedAmount, double actualAmount, double newAmount);
};



#endif /* SRC_MAIN_ISTRATEGY_H_ */
