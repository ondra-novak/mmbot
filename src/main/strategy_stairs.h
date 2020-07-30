/*
 * strategy_stairs.h
 *
 *  Created on: 2. 2. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_STAIRS_H_
#define SRC_MAIN_STRATEGY_STAIRS_H_

#include "istrategy.h"

class Strategy_Stairs: public IStrategy {
public:
	enum Pattern {
		constant,
		arithmetic,
		harmonic,
		exponencial,
		parabolic,
		sqrt,
		poisson1,
		poisson2,
		poisson3,
		poisson4,
		poisson5
	};


	enum TradingMode {
		autodetect,
		exchange,
		margin
	};

	enum ReductionMode {
		stepsBack,
		reverse,
		lockOnReduce,
		lockOnReverse
	};

	struct Config {
		double power;
		double neutral_pos;
		Pattern pattern;
		intptr_t max_steps;
		intptr_t reduction;
		TradingMode mode;
		ReductionMode redmode;
		bool sl;
	};

	struct State {
		double price = 0;
		double pos = 0;
		double open = 0;
		double enter = 0;
		double value = 0;
		double neutral_pos = 0;
		double power = 0;
		intptr_t step = 0;
		intptr_t prevdir = 0;
		std::size_t cfghash = 0;
		bool sl = false;
	};

	Strategy_Stairs(const Config &cfg);
	Strategy_Stairs(const Config &cfg, const State &state);
	virtual ~Strategy_Stairs();
	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency) const;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const;
	virtual bool isValid() const;
	virtual json::Value exportState() const;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const;
	virtual double getEquilibrium(double assets) const;
	virtual PStrategy reset() const;
	virtual std::string_view  getID() const;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const;
	virtual PStrategy importState(json::Value src,const IStockApi::MarketInfo &minfo) const;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual std::optional<BudgetExtraInfo> getBudgetExtraInfo(double price, double currency) const {
		return std::optional<BudgetExtraInfo>();
	}



	static std::string_view id;
	bool isMargin(const IStockApi::MarketInfo& minfo) const;
protected:
	const Config cfg;
	State st;

	double calcPower(double price, double currency) const;
	intptr_t getNextStep(double dir, std::intptr_t prev_dir) const;

	double assetsToPos(double assets) const;
	double posToAssets(double pos) const;
	double stepToPos(std::intptr_t step) const;
	std::intptr_t posToStep(double pos) const;

	double calcNeutralPos(double assets, double currency, double price, bool leverage) const;

	template<typename Fn>
	static void serie(Pattern pat, int maxstep, Fn &&cb);
	std::size_t getCfgHash() const;

};

#endif /* SRC_MAIN_STRATEGY_STAIRS_H_ */
