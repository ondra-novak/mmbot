/*
 * strategy_pile.h
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_PILE_H_
#define SRC_MAIN_STRATEGY_PILE_H_

#include <string_view>

#include "istrategy3.h"
#include "istrategy.h"

class Strategy_Pile: public IStrategy {
public:

	struct Config {
		double ratio; // requested ratio, however this value is used only for backtest
		double accum; // accumulation - 0-1
	};

	struct State {
		double ratio = 0; // Initialized ratio (during reset)
		double kmult = 0; // multiplication constant
		double lastp = 0; // last price
		double budget = 0; // budget at last price
		double pos = 0;
		double berror = 0; //budget error
	};

	Strategy_Pile(const Config &cfg);
	Strategy_Pile(const Config &cfg, State &&st);

	PStrategy init(double price, double assets, double currency, bool leveraged) const;

	static std::string_view id;

	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const;
	virtual bool isValid() const;
	virtual json::Value exportState() const;
	virtual std::string_view getID() const;
	virtual double getCenterPrice(double lastPrice, double assets) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const;
	virtual IStrategy::BudgetInfo getBudgetInfo() const;
	virtual double getEquilibrium(double assets) const;
	virtual double calcCurrencyAllocation(double price) const;
	virtual IStrategy::ChartPoint calcChart(double price) const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual PStrategy reset() const;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const;

	static double calcPosition(double ratio, double kmult, double price);
	static double calcBudget(double ratio, double kmult, double price);
	static double calcEquilibrium(double ratio, double kmul, double position);
	static double calcPriceFromBudget(double ratio, double kmul, double budget);
	static double calcCurrency(double ratio, double kmult, double price);
	static double calcPriceFromCurrency(double ratio, double kmult, double currency);



protected:
	Config cfg;
	State st;

	std::pair<double,double> calcAccum(double new_price) const;
};

class Strategy3_Pile: public IStrategy3 {
public:
	Strategy3_Pile(double ratio);
	Strategy3_Pile(double ratio, double constant);
	virtual ChartPoint get_chart_point(double price) const override;
	virtual json::Value save() const override;
	virtual PStrategy3 run(AbstractTraderControl &cntr) const override;
	virtual PStrategy3 load(const json::Value &state) const override;
	virtual double calc_initial_position(const InitialState &st) const override;
	virtual std::string_view get_id() const override;;

	static std::string_view id;

	virtual PStrategy3 reset() const override;
	static void reg(AbstractStrategyRegister &r);

protected:
	static double calcPos(double r, double c, double price);
	static double calcBudget(double r, double c, double price);
	static double calcAlloc(double r, double c, double price);
	static double calcPriceFromPos(double r, double c, double pos);
	static double calcPriceFromCurrency(double r, double c, double cur);
	static double calcConstant(double r, double price, double eq);

	double ratio = 0;
	double constant = 0;
};

#endif /* SRC_MAIN_STRATEGY_PILE_H_ */
