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


	enum ReduceMode {
		reduceFromProfit,
		fixedReduce,
		neutralMove,
		toOpenPrice,
		ema,
		stableProfit
	};

	struct Config {
		double step;
		double pos_offset;
		double maxpos;
		double reduce_factor;
		double power;
		double baltouse;
		ReduceMode reduceMode;
		bool reduce_on_increase;
	};

	using PMyStrategy = ondra_shared::RefCntPtr<const Strategy_PLFromPos>;

	struct State {
		bool inited = false;
		bool valid_power = false;
		double p = 0;
		double a = 0;
		double k = 0;
		double maxpos = 0;
		double value = 0;
		double avgsum = 0;
		double neutral_pos = 0;
		PMyStrategy suspended;
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

	double assetsToPos(double assets) const;
	double posToAssets(double pos) const;


	double calcK() const;
	static double calcK(const State &st);

	static double sliding_zero_factor;;
	static double min_rp_reduce;

	json::Value cfgStateHash() const;

	bool atMax(const IStockApi::MarketInfo &minfo, const State &st) const;

protected:
	Config cfg;
	State st;

	std::pair<OnTradeResult,PStrategy > onTrade2(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft, bool sub) const;

private:
	double calcNewPos(const IStockApi::MarketInfo &minfo, double tradePrice) const;
	void calcPower(const IStockApi::MarketInfo &minfo, State &st,  double price, double assets, double currency) const;
	bool isAuto() const;
	bool isExchange(const IStockApi::MarketInfo &minfo) const;

	struct CalcNeutralBalanceResult {
		double neutral_pos;
		double balance;
	};
	CalcNeutralBalanceResult calcNeutralBalance(const IStockApi::MarketInfo &minfo, double assets, double currency, double price) const;

	static Config halfConfig(const Config &cfg);
};



#endif /* SRC_MAIN_STRATEGY_PLFROMPOS_H_ */
