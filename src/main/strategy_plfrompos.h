/*
 * strategy_plfrompos.h
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_PLFROMPOS_H_
#define SRC_MAIN_STRATEGY_PLFROMPOS_H_

#include "istrategy.h"

class Strategy_PLFromPos: public IStrategy {
public:
	enum CloseMode {
		always_close,
		prefer_close,
		prefer_reverse
	};



	struct Config {
		double step;
		double accum;
		double neutral_pos;
		double maxpos;
		double reduce_factor;
		double power;
		double baltouse;
		bool fixed_reduce;

	};

	struct State {
		bool inited = false;
		bool valid_power = false;
		double p = 0;
		double a = 0;
		double step = 0;
		double maxpos = 0;
		double acm = 0;
		double value = 0;
	};

	Strategy_PLFromPos(const Config &cfg, const State &st);

	virtual bool isValid() const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;


	static std::string_view id;

	double getNeutralPos(const IStockApi::MarketInfo &minfo) const;
	double assetsToPos(const IStockApi::MarketInfo &minfo, double assets) const;
	double posToAssets(const IStockApi::MarketInfo &minfo, double pos) const;


	double calcK() const;
	static double calcK(const State &st);

protected:
	Config cfg;
	State st;

	std::pair<OnTradeResult,PStrategy > onTrade2(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const;

private:
	double calcNewPos(const IStockApi::MarketInfo &minfo, double tradePrice) const;
	void calcPower(State &st,  double price, double assets, double currency) const;
};

#endif /* SRC_MAIN_STRATEGY_PLFROMPOS_H_ */
