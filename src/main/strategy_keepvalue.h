/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_KEEPVALUE_H_
#define SRC_MAIN_STRATEGY_KEEPVALUE_H_
#include <chrono>

#include "istrategy.h"

class Strategy_KeepValue: public IStrategy {
public:

	struct Config {
		double ea;
		double accum;
		double chngtm;
		bool keep_half;
	};


	struct State {
			bool valid = false;
			double p = 0;
			double a = 0;
			double n = 0;
			std::uint64_t recalc_time = 0;
			std::uint64_t check_time = 0;
		};

	Strategy_KeepValue(const Config &cfg, State &&st);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src,const IStockApi::MarketInfo &minfo) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency, bool rej) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo & , double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual std::optional<IStrategy::BudgetExtraInfo> getBudgetExtraInfo(double price, double currency) const override;


	static std::string_view id;

protected:
	Config cfg;
	State st;


	double calcK() const;
	static double calcK(const State &st, const Config &cfg);
	static double calcReqCurrency(const State &st, const Config &cfg, double price);
	static double calcA(const State &st, const Config &cfg, double price);
	static double calcAccumulation(const State &st, const Config &cfg, double price, double currencyLeft);
	static double calcNormalizedProfit(const State &st, const Config &cfg, double tradePrice, double tradeSize);

};


#endif /* SRC_MAIN_STRATEGY_KEEPVALUE_H_ */
