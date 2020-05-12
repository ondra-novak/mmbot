/*
 * strategy_leveraged_base.h
 *
 *  Created on: 9. 5. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_LEVERAGED_BASE_H_
#define SRC_MAIN_STRATEGY_LEVERAGED_BASE_H_




template<typename Calc>
class Strategy_Leveraged: public IStrategy {
public:

	struct Config {
		double power;
		double asym;
		double max_loss;
		double reduction;
		double external_balance;
		double powadj;
		double dynred;
		bool detect_trend;
		int preference;
	};


	struct State {
		double neutral_price = 0;
		double last_price =0;
		double position = 0;
		double bal = 0;
		double val = 0;
		double power = 0;
		long trend_cntr = 0;
	};

	Strategy_Leveraged(const Config &cfg, State &&st);
	Strategy_Leveraged(const Config &cfg);

	virtual bool isValid() const override;
	virtual PStrategy  onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult,PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo,  double cur_price,double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual std::string_view getID() const override {return id;}

	static std::string_view id;

protected:
	Config cfg;
	State st;
	mutable std::optional<MinMax> rootsCache;

	struct PosCalcRes {
		bool limited;
		double pos;
	};

	static Strategy_Leveraged init(const Config &cfg, double price, double pos, double currency, bool futures);
	PosCalcRes calcPosition(double price) const;

	MinMax calcRoots() const;
	double adjNeutral(double price, double value) const;

	double calcMaxLoss() const;
	double calcMult() const;


	double calcNewNeutralFromProfit(double profit, double price) const;

private:
	static void recalcPower(const Config &cfg, State &nwst) ;
	static void recalcNeutral(const Config &cfg, State &nwst) ;
	json::Value storeCfgCmp() const;
	static void recalcNewState(const Config &cfg, State &nwst);
	double calcAsym() const;
	static double calcAsym(const Config &cfg, const State &st) ;
	static double trendFactor(const State &st);
};


#endif /* SRC_MAIN_STRATEGY_LEVERAGED_BASE_H_ */
