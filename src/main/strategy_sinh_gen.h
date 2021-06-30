/*
 * strategy_sinh_gen.h
 *
 *  Created on: 27. 5. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_SINH_GEN_H_
#define SRC_MAIN_STRATEGY_SINH_GEN_H_

#include "../imtjson/src/imtjson/value.h"
#include "strategy.h"

class Strategy_Sinh_Gen: public IStrategy {
public:


	using Point = std::pair<double, double>;

	class FnCalc {
	public:
		FnCalc(double wd);

		double baseFn(double x) const;
		double integralBaseFn(double x) const;
		double assets(double k, double w, double x) const;
		double budget(double k, double w, double x) const;

		static bool sortPoints(const Point &a, const Point &b);

		const double getWD() const {return wd;}

	protected:
		double wd;
		std::vector<Point> itable;

	};

	using PFnCalc = std::shared_ptr<FnCalc>;

	struct Config {
		double power;
		PFnCalc calc;
		int disableSide;  //-1 disable short, 1 disable long
		bool reinvest;
		bool avgspread;
		std::size_t calcConfigHash() const;
	};

	struct State {
		bool spot = false;
		double k = 0;
		double p = 0;
		double budget=0;
		double last_spread=1.01;
		double sum_spread=0;
		int trades=0;
	};


	Strategy_Sinh_Gen(const Config &cfg);
	Strategy_Sinh_Gen(const Config &cfg, State &&st);
	virtual bool isValid() const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const override;
	virtual std::pair<IStrategy::OnTradeResult,PStrategy> onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const
					override;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const override;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const override;
	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const override;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;


	static const std::string_view id;

	virtual double getCenterPrice(double lastPrice, double assets) const
			override;
	virtual IStrategy::ChartPoint calcChart(double price) const override;
	virtual double calcCurrencyAllocation(double price) const override;
	virtual IStrategy::BudgetInfo getBudgetInfo() const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const override;

protected:

	Config cfg;
	State st;
	double pw;

	PStrategy init(const IStockApi::MarketInfo &minfo, double price, double pos, double currency) const;
	double calcNewK(double tradePrice, double cb, double pnl) const;
	double limitPosition(double pos) const;

};


#endif /* SRC_MAIN_STRATEGY_SINH_GEN_H_ */
