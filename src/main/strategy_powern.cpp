#include "strategy_powern.h"
#include "numerical.h"
#include <imtjson/object.h>

#include "sgn.h"
//smallest value - default count of steps of find_root is 32, so smallest unit is aprx 1e-10
constexpr double epsilon = 1e-14;

template<typename BaseFn>
Strategy_DCAM<BaseFn>::Strategy_DCAM(Config cfg):_cfg(cfg),_state{} {}
template<typename BaseFn>
Strategy_DCAM<BaseFn>::Strategy_DCAM(Config cfg, State state):_cfg(cfg),_state(state) {}


template<typename BaseFn>
double Strategy_DCAM<BaseFn>::find_k(double c, double p, double price,double val, double pos) const {
    if (val >= epsilon) return price;
    return not_nan(Numerics<>::find_root_pos(price, pos,[&](double k){
        return _cfg.baseFn.integral_fnx(p, k, c, price) - val;
    }),price);
}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::find_k_from_pos(double c, double p,double price, double pos) const {
    if (std::abs(pos) < epsilon) return price;
    return not_nan(Numerics<>::find_root_pos(price, pos,[&](double k){
        return _cfg.baseFn.fnx(p, k, c, price) - pos;
    }),price);

}


double FunctionPowerN::fnx(double p, double k, double c, double x) const {
    double xk = x/k;
    return (w*p*c)/(2*k*w*w)*(std::pow(xk,-w)-std::pow(xk,w));

}

double FunctionPowerN::integral_fnx(double p, double k, double c, double x) const {
    double xk = x/k;
    return -(c*p*(-2*k*w+(1+w)*x*std::pow(xk,-w)+(w-1)*x*std::pow(xk,w))/(2*k*(w-1)*w*(w+1)));
}

double FunctionPowerN::invert_fnx(double p, double k, double c,double x) const {
    return k*std::pow((std::sqrt(c*c*p*p+k*k*x*x*w*w) -  k*x*w)/(c*p),1.0/w);
}

template<typename BaseFn>
typename Strategy_DCAM<BaseFn>::RuleResult Strategy_DCAM<BaseFn>::find_k_rule(double new_price, bool alert) const {
    double aprx_pnl = _state._pos * (new_price - _state._p);
    double new_val = _state._val + aprx_pnl;
    double new_k = _state._k;
    double spread = _state._k * new_price/_state._p - _state._k;
    if ((_state._p -_state._k) * (new_price - _state._k) < 0) {
            new_k = new_price;
    } else {
        if (alert) {
            new_k = find_k(_cfg, new_price, -std::sqrt(new_val * _state._val), _state._pos);
        }else if (aprx_pnl < 0 ) {
            new_k = find_k(_cfg, new_price, new_val, _state._pos);
        } else  {
                double y = _state._pos?_cfg.yield_mult:_cfg.initial_yield_mult;
                double extra_val = calc_value(_cfg, _state._k, _state._k + spread * std::abs(y)) * sgn(y);
                new_val += extra_val;
                new_k = find_k(_cfg, new_price, new_val, _state._pos?_state._pos:(_state._p - new_price));
        }
        if (_state._pos && ((new_k - _state._k) * (new_price - _state._k)< 0)) {
            new_k = _state._k;
        }
    }
    return {
        new_k,
        calc_value(_cfg, new_k, new_price),
        calc_position(_cfg,new_k, new_price)
    };
}

template<typename BaseFn>
bool Strategy_DCAM<BaseFn>::isValid() const {
    return _state._k > 0 && _state._p > 0;
}

template<typename BaseFn>
PStrategy Strategy_DCAM<BaseFn>::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
    State st = {
            src["val"].getNumber(),
            src["k"].getNumber(),
            src["p"].getNumber(),
            src["pos"].getNumber()
    };
    return PStrategy(new Strategy_DCAM<BaseFn>(_cfg, st));
}

template<typename BaseFn>
json::Value Strategy_DCAM<BaseFn>::exportState() const {
    return json::Object{
        {"val", _state._val},
        {"k",_state._k},
        {"p",_state._p},
        {"pos", _state._pos}
    };
}

template<typename BaseFn>
json::Value Strategy_DCAM<BaseFn>::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
    return json::Object {};
}

template<typename BaseFn>
PStrategy Strategy_DCAM<BaseFn>::init(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const {
    State st;
    st._k = find_k_from_pos(_cfg, price, assets);
    st._val = calc_value(_cfg, st._k, price);
    st._p = price;
    st._pos = assets;
    PStrategy out(new Strategy_DCAM<BaseFn>(_cfg, st));
    if (!out->isValid()) throw std::runtime_error("Unable to initialize strategy");
    return out;
}

template<typename BaseFn>
IStrategy::OrderData Strategy_DCAM<BaseFn>::getNewOrder(
        const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
        double dir, double assets, double currency, bool rej) const {

    if (!Strategy_DCAM<BaseFn>::isValid()) return init(minfo, cur_price, assets, currency)->getNewOrder(minfo, cur_price, new_price, dir, assets, currency, rej);

    double ord =calc_order(new_price, dir)*dir;
    return {0, ord, Alert::enabled};

}

template<typename BaseFn>
std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_DCAM<BaseFn>::onTrade(
        const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
        double assetsLeft, double currencyLeft) const {

    if (!Strategy_DCAM<BaseFn>::isValid()) return init(minfo, tradePrice, assetsLeft-tradeSize, currencyLeft)
                ->onTrade(minfo, tradePrice, tradeSize, assetsLeft, currencyLeft);

    if (std::abs(assetsLeft) < minfo.calcMinSize(tradePrice)) {
        assetsLeft = 0;
    }
    RuleResult r = find_k_rule(tradePrice,!tradeSize);
    double new_price = tradePrice;
    if (tradeSize) {
        new_price = find_price_from_pos(_cfg, r.k, assetsLeft);
        r = find_k_rule(tradePrice);
    }
    State new_state;
    new_state._val = r.val;
    new_state._k = r.k;
    new_state._p = new_price;
    new_state._pos = assetsLeft;

    double pnl = (tradePrice - _state._p) * (assetsLeft - tradeSize);

    double np = _state._val - r.val + pnl;
    return {
        {np, 0, new_state._k, 0},
        PStrategy(new Strategy_DCAM<BaseFn>(_cfg, new_state))
    };


}

template<typename BaseFn>
PStrategy Strategy_DCAM<BaseFn>::onIdle(const IStockApi::MarketInfo &minfo,
        const IStockApi::Ticker &curTicker, double assets,
        double currency) const {
    if (!Strategy_DCAM<BaseFn>::isValid()) return init(minfo, curTicker.last, assets, currency)
                ->onIdle(minfo, curTicker, assets, currency);
    return PStrategy(this);
}

template<typename BaseFn>
PStrategy Strategy_DCAM<BaseFn>::reset() const {
    return PStrategy(new Strategy_DCAM<BaseFn>(_cfg));
}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::calcInitialPosition(const IStockApi::MarketInfo &, double , double , double ) const {
    return 0;
}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::getCenterPrice(double lastPrice, double assets) const {
    return Strategy_DCAM<BaseFn>::getEquilibrium(assets);
}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::getEquilibrium(double assets) const {
    return find_price_from_pos(_cfg, _state._k, assets);
}

template<typename BaseFn>
IStrategy::MinMax Strategy_DCAM<BaseFn>::calcSafeRange(
        const IStockApi::MarketInfo &minfo, double assets,
        double currencies) const {

    if (minfo.leverage) {

        double budget = currencies - _state._val;
        double min_val = Numerics<15>::find_root_to_zero(_state._k, [&](double x){
            return calc_value(_cfg, _state._k, x) + budget;
        });
        double max_val = Numerics<15>::find_root_to_inf(_state._k, [&](double x){
            return calc_value(_cfg, _state._k, x) + budget;
        });
        return {min_val, max_val};
    } else {
        double budget = currencies + assets*_state._p - _state._val;
        double min_val = Numerics<15>::find_root_to_zero(_state._k, [&](double x){
            return calc_value(_cfg, _state._k, x) + calc_position(_cfg, _state._k, x)*x + budget;
        });
        return {min_val, _state._k};
    }

}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::calcCurrencyAllocation(double price, bool leveraged) const {
    if (leveraged) {
        return calc_value(_cfg, _state._k, price) + _cfg.initial_budget;
    } else {
        return _state._val + _cfg.initial_budget - _state._p * _state._pos;
    }
}

template<typename BaseFn>
std::string_view Strategy_DCAM<BaseFn>::getID() const {
    return Strategy_DCAM<BaseFn>::id;
}

template<typename BaseFn>
IStrategy::BudgetInfo Strategy_DCAM<BaseFn>::getBudgetInfo() const {
    return {
        _cfg.initial_budget + _state._val,
        _state._pos
    };
}

template<typename BaseFn>
IStrategy::ChartPoint Strategy_DCAM<BaseFn>::calcChart(double price) const {
    return {
        true,
        calc_position(_cfg, _state._k, price),
        calc_value(_cfg, _state._k, price) + _cfg.initial_budget
    };
}

template<typename BaseFn>
double Strategy_DCAM<BaseFn>::calc_order(double price, double side) const {
    RuleResult r = find_k_rule(price);
    double apos = r.pos * side;
    double diff = apos - _state._pos * side;
    return diff;
}

double FunctionSinH::fnx(double p, double k, double c, double x) const {
    return (w/k) * std::sinh(w * (1 - x/k)) * p * c/(w*w);
}

double FunctionSinH::integral_fnx(double p, double k, double c, double x) const {
    return  (1 - std::cosh(w*(1 - x/k))) * p * c/(w*w);
}
double FunctionSinH::invert_fnx(double p, double k, double c, double x) const {
    return k - (k * asinh((k * x)/(p * c/w )))/w;
}


template class Strategy_DCAM<FunctionSinH>;
template class Strategy_DCAM<FunctionPowerN> ;

double FunctionVolumeSinH::fnx(double p, double k, double c, double x) const {
    return p* (c/(x*w)) * std::sinh(w*(1 - x / k));
}

double FunctionVolumeSinH::integral_fnx(double p, double k, double c, double x) const {
    return p* (c / w) * integral_table(x / k);
}

double FunctionVolumeSinH::invert_fnx(double p, double k, double c, double x) const {
    if (x > 0) {
        return Numerics<50>::find_root_to_zero(k,[&](double v){
           return fnx(p,k,c,v) - x;
        });
    } else if (x < 0) {
        return Numerics<50>::find_root_to_inf(k,[&](double v){
           return fnx(p,k,c,v) - x;
        });
    } else {
        return k;
    }
}

FunctionSinH::FunctionSinH(double w):w(w) {}

FunctionPowerN::FunctionPowerN(double w):w(w) {}

FunctionVolumeSinH::FunctionVolumeSinH(double w):w(w) {
    double a = invert_fnx(1, 1, w, 1e10);
    double b = invert_fnx(1, 1, w, -1e10);
    integral_table = numeric_integral([this,w](double x)->double{
        return fnx(1,1,w,x);
    }, a, b, 1.0, 17);
}



PStrategy create_DCAM(DCAM_type type, double multiplier, double initial_budget,
        double initial_yield_mult, double yield_mult, double power) {

    auto create = [&](auto basefn) {
        using BaseFN = decltype(basefn);
        typename Strategy_DCAM<BaseFN>::Config cfg = {
                multiplier,
                initial_budget,
                initial_yield_mult,
                yield_mult,
                basefn
        };
        return PStrategy(new Strategy_DCAM<BaseFN>(cfg));
    };

    switch (type) {
        default:
        case DCAM_type::sinh: return create(FunctionSinH(power));
        case DCAM_type::powern: return create(FunctionPowerN(power));
        case DCAM_type::volume_sinh: return create(FunctionVolumeSinH(power));
    }

}
