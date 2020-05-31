/*
 * strategy_keepvalue.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_EXPONENCIAL_H_
#define SRC_MAIN_STRATEGY_EXPONENCIAL_H_
#include <chrono>

#include "istrategy.h"

class Strategy_Exponencial: public IStrategy {
public:

	struct Config {
		double ea;
		double accum;
	};


	struct State {
			bool valid = false;
			double w = 0;
			double k = 0;
			double p = 0;
			double f = 0; //fiat
		};

	Strategy_Exponencial(const Config &cfg, State &&st);
	Strategy_Exponencial(const Config &cfg);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;

	static std::string_view id;


	static double calcA(const State &st, double price);
	static void updateState(State &st, double new_a, double new_p, double new_f);
	static double calcAccountValue(const State &st);
	static double calcReqCurrency(const State &st, double price);
	static Strategy_Exponencial init(const Config &cfg, double price, double assets, double cur);
protected:
	Config cfg;
	State st;


	struct NormProfit {
		double np;
		double na;

	};

	NormProfit calcNormalizedProfit(double tradePrice, double tradeSize) const;
	static double findRoot(double w, double k, double p, double c);
};


#endif /* SRC_MAIN_STRATEGY_EXPONENCIAL_H_ */
