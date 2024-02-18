/*
 * strategy_powern.h
 *
 *  Created on: 18. 2. 2024
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_POWERN_H_
#define SRC_MAIN_STRATEGY_POWERN_H_

#include "istrategy.h"


class Strategy_PowerN : public IStrategy {
public:

    static constexpr std::string_view id = "power_n";

    struct Config {
        double power; //w
        double multiplier; //c
        double initial_budget; //p
        double initial_yield_mult;
        double yield_mult;
    };

    struct State {
        double _val = 0;     //current value/loss
        double _k = 0;       //previous k value;
        double _p = 0;       //reference price
        double _pos = 0;
    };

    virtual bool isValid() const override;


    Strategy_PowerN(Config cfg);
    Strategy_PowerN(Config cfg, State state);

    virtual IStrategy::OrderData getNewOrder(const IStockApi::MarketInfo &minfo,
            double cur_price, double new_price, double dir, double assets,
            double currency, bool rej) const override;
    virtual std::pair<IStrategy::OnTradeResult,PStrategy> onTrade(
            const IStockApi::MarketInfo &minfo, double tradePrice,
            double tradeSize, double assetsLeft, double currencyLeft) const
                    override;
    virtual PStrategy importState(json::Value src,
            const IStockApi::MarketInfo &minfo) const override;
    virtual IStrategy::MinMax calcSafeRange(const IStockApi::MarketInfo &minfo,
            double assets, double currencies) const override;
    virtual json::Value exportState() const override;
    virtual double calcCurrencyAllocation(double price, bool leveraged) const override;
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
    virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;

protected:

    Config _cfg;
    State _state;

    double find_k(double w, double c, double p, double price, double val, double pos) const;
    double find_k_from_pos(double w, double c, double p, double price, double pos) const;

    static double fnx(double p, double w, double k, double c, double x);
    static double integral_fnx(double p, double w, double k, double c, double x);
    static double invert_fnx(double p, double w, double k, double c, double x);

    double calc_position(const Config &cfg, double k, double x) const {
        return fnx(cfg.initial_budget, cfg.power, k, cfg.multiplier, x);
    }
    double calc_value(const Config &cfg, double k, double x) const {
        return integral_fnx(cfg.initial_budget, cfg.power, k, cfg.multiplier, x);
    }
    double find_price_from_pos(const Config &cfg, double k, double x) const {
        return invert_fnx(cfg.initial_budget, cfg.power, k, cfg.multiplier, x);
    }

    double find_k(const Config &cfg, double price, double val, double pos) const{
        return find_k(cfg.power, cfg.multiplier, cfg.initial_budget, price, val, pos);
    }
    double find_k_from_pos(const Config &cfg, double price, double pos) const{
        return find_k_from_pos(cfg.power, cfg.multiplier, cfg.initial_budget, price, pos);
    }


    struct RuleResult {
        double k = 0.0;
        double val = 0.0;
        double pos = 0.0;
    };

    RuleResult  find_k_rule(double new_price, bool alert = false) const;

    PStrategy init(const IStockApi::MarketInfo &minfo,double price, double assets, double currency) const;

    double calc_order(double price, double side) const;



};





#endif /* SRC_MAIN_STRATEGY_POWERN_H_ */
