/*
 * strategy_exponencial.h
 *
 *  Created on: 27. 8. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_EXPONENCIAL_H_
#define SRC_MAIN_STRATEGY_EXPONENCIAL_H_

#include "istrategy.h"


class Strategy_Exponencial: public IStrategy {
public:

    static const std::size_t intTableSize = 10000;
    using IntTable = std::array<double, intTableSize+1>; //static integration table

    struct Config {
        double z;       //exp - exponent
        double w;       //denominator exponent
        double r;       //ratio at reference point
        double s;       //shrink ratio
    };

    struct MathModule: Config {
        double range;   //integration range - calculated
        IntTable int_table; //integration table;
        static double findApxRangeEnd(double z);
        void initTable(double min, double max);
        double baseFn(double x) const;
        double baseFnEx(double x) const;
        double primFn(double x) const;
        double primFnEx(double x) const;
        double invFn(double y) const;
        double invFnEx(double y) const;
        double calcPos(double x, double k, double w) const;
        double calcEquity(double x, double k, double w) const;
        double findPos(double y, double k, double w) const;
        double calcCurr(double x, double k, double w) const;
        double findCurrency(double c, double k, double w) const;
        double calcRange(double k) const;
        double calcRatio(double x, double k) const;
        double findRatio(double r, double k) const;
        double findEquity(double eq, double k, double w) const;

    };

    using PMathModule = std::shared_ptr<const MathModule>;

    static PMathModule prepareMath(const Config &cfg);


    struct State {
        bool spot = true;
        double k = 0;
        double p = 0;
        double b = 0;
        double m = 0;
    };


    Strategy_Exponencial(const Config &cfg);
    Strategy_Exponencial(const PMathModule &math, State &&st);


    //https://www.desmos.com/calculator/aebvfgxzpk



public:
    virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
            double cur_price, double new_price, double dir, double assets,
            double currency, bool rej) const override;
    virtual std::pair<IStrategy::OnTradeResult,PStrategy > onTrade(
            const IStockApi::MarketInfo &minfo, double tradePrice,
            double tradeSize, double assetsLeft, double currencyLeft) const
                    override;
    virtual PStrategy importState(json::Value src,
            const IStockApi::MarketInfo &minfo) const override;
    virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
            double assets, double currencies) const override;
    virtual bool isValid() const override;
    virtual json::Value exportState() const override;
    virtual double calcCurrencyAllocation(double price, bool leveraged) const
            override;
    virtual std::string_view getID() const override;
    virtual double getCenterPrice(double lastPrice, double assets) const
            override;
    virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo,
            double price, double assets, double currency) const override;
    virtual IStrategy::BudgetInfo getBudgetInfo() const override;
    virtual double getEquilibrium(double assets) const override;
    virtual IStrategy::ChartPoint calcChart(double price) const override;
    virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo,
            const IStockApi::Ticker &curTicker, double assets,
            double currency) const override;
    virtual PStrategy reset() const override;
    virtual json::Value dumpStatePretty(
            const IStockApi::MarketInfo &minfo) const override;

    static std::string_view id;

protected:

    PMathModule mcfg;
    State st;

    PStrategy init(bool spot, double price, double pos, double equity) const;

};


#endif /* SRC_MAIN_STRATEGY_EXPONENCIAL_H_ */
