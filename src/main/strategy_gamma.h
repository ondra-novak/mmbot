/*
 * strategy_gamma.h
 *
 *  Created on: 20. 5. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_GAMMA_H_
#define SRC_MAIN_STRATEGY_GAMMA_H_

#include "istrategy.h"

class Strategy_Gamma: public IStrategy {
public:

	enum Function {
		halfhalf,
		keepvalue,
		exponencial,
		gauss,
		invsqrtsinh
	};

	struct IntegrationTable {
		Function fn;
		double z;
		double a;
		double b;
		std::vector<std::pair<double, double> > values;
		IntegrationTable(Function fn, double z);

		double get(double x) const;
		double get_max() const;
		double get_min() const;

		double mainFunction(double x) const;
		double calcAssets(double k, double w, double x) const;
		double calcBudget(double k, double w, double x) const;
		double calcCurrency(double k, double w, double x) const;


	};


	struct Config {
		std::shared_ptr<IntegrationTable> intTable;
		int reduction_mode;
		double trend;
		bool reinvest;
		bool maxrebalance;
		json::String calcConfigHash() const;
	};


	struct State {
		double k = 0;
		double w = 0;
		double p = 0;
		double b = 0;
		double d = 0;
		double uv = 0; //unprocessed volume
		double kk = 0;
	};

	Strategy_Gamma(const Config &cfg);
	Strategy_Gamma(const Config &cfg, State &&st);
	Strategy_Gamma(Strategy_Gamma &&other);




	virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
			double cur_price, double new_price, double dir, double assets,
			double currency, bool rej) const override;
	virtual std::pair<IStrategy::OnTradeResult,
			ondra_shared::RefCntPtr<const IStrategy> > onTrade(
			const IStockApi::MarketInfo &minfo, double tradePrice,
			double tradeSize, double assetsLeft, double currencyLeft) const
					override;
	virtual PStrategy importState(json::Value src,
			const IStockApi::MarketInfo &minfo) const override;
	virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
			double assets, double currencies) const override;
	virtual bool isValid() const override;
	virtual json::Value exportState() const override;
	virtual std::string_view getID() const override;
	virtual double getCenterPrice(double lastPrice, double assets) const
			override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const override;
	virtual IStrategy::BudgetInfo getBudgetInfo() const override;
	virtual double getEquilibrium(double assets) const override;
	virtual double calcCurrencyAllocation(double price) const override;
	virtual IStrategy::ChartPoint calcChart(double price) const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
			const IStockApi::Ticker &curTicker, double assets,
			double currency) const override;
	virtual PStrategy reset() const override;
	virtual json::Value dumpStatePretty(
			const IStockApi::MarketInfo &minfo) const override;

	static const std::string_view id;

protected:

	double calculatePosition(double a,double price, double minsize) const;
	Strategy_Gamma init(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const;
	double calculateCurPosition() const;

	struct NNRes {
		double k;
		double w;
	};
	NNRes calculateNewNeutral(double a, double price, double min_order_size) const;

	double calibK(double k) const;

	Config cfg;
	State state;

};

#endif /* SRC_MAIN_STRATEGY_GAMMA_H_ */
