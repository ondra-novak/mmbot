/*
 * strategy.h
 *
 *  Created on: 17. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_H_
#define SRC_MAIN_STRATEGY_H_

#include "istrategy.h"

class Strategy {
public:

	using Ptr = ondra_shared::RefCntPtr<IStrategy>;
	using TradeResult = IStrategy::OnTradeResult;
	using MinMax = IStrategy::MinMax;

	Strategy(const Ptr ptr):ptr(ptr) {}
	bool isValid() const {return ptr!=nullptr && ptr->isValid();}
	void init(double curPrice, double assets, double currency)  {
		ptr = ptr->init(curPrice, assets, currency);
	}
	void setMarketInfo(const IStockApi::MarketInfo &minfo) {
		ptr = ptr->setMarketInfo(minfo);
	}
	TradeResult onTrade(double tradePrice, double tradeSize,
			double assetsLeft, double currencyLeft)  {
		auto t = ptr->onTrade(tradePrice, tradeSize, assetsLeft, currencyLeft);
		ptr = t.second;
		return t.first;
	}
	json::Value exportState() const;
	void importState(json::Value src);

	double calcOrderSize(double price, double assets) const {
		return ptr->calcOrderSize(price, assets);
	}
	MinMax calcSafeRange(double assets, double currencies) const {
		return ptr->calcSafeRange(assets, currencies);
	}
	double getEquilibrium() const {
		return ptr->getEquilibrium();
	}
	void reset() {
		ptr = ptr->reset();
	}


	static Strategy create(std::string_view id, json::Value config);

protected:
	Ptr ptr;



};


#endif /* SRC_MAIN_STRATEGY_H_ */
