/*
 * strategy_powern.h
 *
 *  Created on: 18. 2. 2024
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY_POWERN_H_
#define SRC_MAIN_STRATEGY_POWERN_H_

#include "istrategy.h"
#include "integral.h"


template<typename BaseFn>
class Strategy_DCAM : public IStrategy {
public:

    static constexpr std::string_view id = "DCAM";

    struct Config {
        double multiplier; //c
        double initial_budget; //p
        double initial_yield_mult;
        double yield_mult;
        BaseFn baseFn;
    };

    struct State {
        double _val = 0;     //current value/loss
        double _k = 0;       //previous k value;
        double _p = 0;       //reference price
        double _pos = 0;
    };

    virtual bool isValid() const override;


    Strategy_DCAM(Config cfg);
    Strategy_DCAM(Config cfg, State state);

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

    double find_k( double c, double p, double price, double val, double pos) const;
    double find_k_from_pos(double c, double p, double price, double pos) const;

    double calc_position(const Config &cfg, double k, double x) const {
        return _cfg.baseFn.fnx(cfg.initial_budget, k, cfg.multiplier, x);
    }
    double calc_value(const Config &cfg, double k, double x) const {
        return _cfg.baseFn.integral_fnx(cfg.initial_budget, k, cfg.multiplier, x);
    }
    double find_price_from_pos(const Config &cfg, double k, double x) const {
        return _cfg.baseFn.invert_fnx(cfg.initial_budget, k, cfg.multiplier, x);
    }

    double find_k(const Config &cfg, double price, double val, double pos) const{
        return find_k(cfg.multiplier, cfg.initial_budget, price, val, pos);
    }
    double find_k_from_pos(const Config &cfg, double price, double pos) const{
        return find_k_from_pos(cfg.multiplier, cfg.initial_budget, price, pos);
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

struct FunctionSinH {
    double w = 1.0;
    double fnx(double p, double k, double c, double x) const;
    double integral_fnx(double p, double k, double c, double x) const;
    double invert_fnx(double p, double k, double c, double x) const;

    FunctionSinH() = default;
    FunctionSinH(double w);
};
struct FunctionPowerN {
    double w = 1.0;
    double fnx(double p, double k, double c, double x) const;
    double integral_fnx(double p, double k, double c, double x)const;
    double invert_fnx(double p, double k, double c, double x)const;

    FunctionPowerN() = default;
    FunctionPowerN(double w);
};

struct FunctionVolumeSinH {

    double w;
    NumericIntegralT<double, double> integral_table;
    double fnx(double p, double k, double c, double x) const;
    double integral_fnx(double p, double k, double c, double x)const;
    double invert_fnx(double p, double k, double c, double x)const;

    FunctionVolumeSinH() = default;
    FunctionVolumeSinH(double w);

};

using Strategy_PowerN = Strategy_DCAM<FunctionPowerN>;

enum class DCAM_type {
    sinh,
    powern,
    volume_sinh
};


PStrategy create_DCAM(DCAM_type type, double multiplier,
        double initial_budget, double initial_yield_mult,
        double yield_mult, double power);




#endif /* SRC_MAIN_STRATEGY_POWERN_H_ */
