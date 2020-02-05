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
		exponencial
	};

	struct Config {
		double power;
		double neutral_pos;
		Pattern pattern;
		intptr_t max_steps;
		bool zero_step;
		bool close_on_reverse;
	};

	struct State {
		double price = 0;
		double pos = 0;
		double open = 0;
		double value = 0;
		double neutral_pos = 0;
		double power = 0;
		intptr_t step = 0;
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
	virtual double getEquilibrium() const;
	virtual PStrategy reset() const;
	virtual std::string_view  getID() const;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const;
	virtual PStrategy importState(json::Value src) const;


	static std::string_view id;
protected:
	const Config cfg;
	State st;

	double calcPower(double price, double currency) const;
	intptr_t getNextStep(double dir) const;

	double assetsToPos(double assets) const;
	double posToAssets(double pos) const;
	double calcPattern(std::intptr_t step) const;

	double calcNeutralPos(double assets, double currency, double price) const;

};

#endif /* SRC_MAIN_STRATEGY_STAIRS_H_ */
