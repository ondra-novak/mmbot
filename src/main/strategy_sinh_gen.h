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
		FnCalc(double wd, double boost, double z);

		double baseFn(double x) const;
		double root(double x) const;
		double root(double k, double w, double x) const;
		double root_of_k(double p, double w, double x) const;
		double integralBaseFn(double x) const;
		double assets(double k, double w, double x) const;
		double budget(double k, double w, double x) const;

		static bool sortPoints(const Point &a, const Point &b);

		const double getWD() const {return wd;}
		const double getBoost() const {return boost;}

	protected:
		double wd;
		double boost;
		double z;
		std::vector<Point> itable;

	};

	using PFnCalc = std::shared_ptr<FnCalc>;

	struct Config {
		double power;
		PFnCalc calc;
		int disableSide;  //-1 disable short, 1 disable long
		double openlimit;
		double ratio;
		bool reinvest;
		bool avgspread;
//		bool lazyopen;  //lazy open and lazy close has been disabled
//		bool lazyclose;
		int boostmode;
		double custom_spread;
	};

	struct State {
		bool spot = false;
		bool use_last_price = false;
		bool rebalance = false; //pokud je true, pak neprovadi redukci ani jine nutne vypocty
		bool at_zero = false; //if set, then strategy is at zero, some calculations are skipped
		double k = 0;
		double p = 0;       //price of last trade
        double p2 = 0;      //price of last trade or alert - when trading was skipped because alert
		double budget=0;
		double pwadj = 1;
		double val = 0;
		double avg_spread=-1;
		double offset = 0;
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
	virtual double calcCurrencyAllocation(double price, bool leveraged) const override;
	virtual IStrategy::BudgetInfo getBudgetInfo() const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
			double price, double assets, double currency) const override;

protected:

	Config cfg;
	State st;
	double pw;

	PStrategy init(const IStockApi::MarketInfo &minfo, double price, double pos, double currency) const;
	double calcNewK(double tradePrice, double cb, double pnl, int bmode) const;
	double limitPosition(double pos) const;
	double adjustPower(double a, double newk, double price) const;
	static double calcPower(const Config &cfg, const State &st);
	static double calcPower(const Config &cfg,  const State &st, double k);
	static double calcNewKFromValue(const Config &cfg, const State &st, double tradePrice, double pw, double enf_val);
	double getEquilibrium_inner(double assets) const;
	double roundZero(double assetsLeft, const IStockApi::MarketInfo &minfo,
			double tradePrice) const;

	double calcRealPosValue(double k, double pw, double price, double pos) const;

	struct NewPosInfo {
	    double pnl;
	    double newk;
	    double newpos;
	    double newpw;
	    double newpwadj;
	    double newofs;
	    double pilekmul;
	    bool is_close;
	};

	NewPosInfo calcNewPos(const IStockApi::MarketInfo &minfo, double new_price, double assets, bool alert) const;

    static double calcPilePosition(double ratio, double kmult, double price);
    static double calcPileBudget(double ratio, double kmult, double price);
    static double calcPileEquilibrium(double ratio, double kmul, double position);
    static double calcPilePriceFromBudget(double ratio, double kmul, double budget);
    static double calcPileCurrency(double ratio, double kmult, double price);
    static double calcPilePriceFromCurrency(double ratio, double kmult, double currency);
    static double calcPileKMult(double price, double budget, double ratio);

};


#endif /* SRC_MAIN_STRATEGY_SINH_GEN_H_ */
