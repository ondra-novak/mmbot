#include "strategy_trending.h"
#include <stdexcept>
#include <imtjson/array.h>
#include <imtjson/object.h>
#include "sgn.h"

Strategy_Trending::Strategy_Trending(const Config &cfg) : cfg(std::make_shared<const Config>(cfg)), state() {}
Strategy_Trending::Strategy_Trending(const std::shared_ptr<const Config > &cfg, State &&state) : cfg(cfg), state(std::move(state)) {}


double Strategy_Trending::calc_ema(double prev_ema, double cur_value, int interval) {
    double m = 2.0/(interval+1);
    return (cur_value - prev_ema)*m + prev_ema;
}

double Strategy_Trending::get_trend() const {
    if (state.ema_history.empty()) return 0;
    return get_trend(state.ema_history.front());
}

double Strategy_Trending::get_trend(double price) const {
    if (state.ema_history.size() < cfg->ema_compare_history) return 0;
    double zone = std::abs(cfg->histersis_percent * state.last_trade_price);
    double cmp_price = state.ema_history.back();

    double diff = price - cmp_price;
    if (std::abs(diff) < zone) return cfg->histersis_percent < 0?0:state.previous_trend;
    else return state.spot?std::max(0,sgn(diff)):sgn(diff);
   
}

bool Strategy_Trending::isValid() const{
    return state.budget > 0;
}
PStrategy Strategy_Trending::onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const{
    if (!this->isValid()) {
        auto s = init_strategy(minfo.leverage > 0, curTicker.bid, assets, currency, curTicker.time);
        if (s->isValid()) return s->onIdle(minfo, curTicker, assets, currency);
        else throw std::runtime_error("Can't initialize strategy");
    } else {
        return this;
    }

}
std::pair<Strategy_Trending::OnTradeResult, PStrategy > Strategy_Trending::onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const{

    if (!this->isValid()) return {{},this};

    auto loc = getLocationInfo(tradePrice);
    State nwstate = state;
    int dir = static_cast<int>(sgn(tradeSize));

    nwstate.last_trade_price = tradePrice;
    nwstate.total_loss = loc.new_loss;
    nwstate.position = assetsLeft;
    nwstate.loss_position = loc.new_rev_pos;    
    nwstate.previous_trend = loc.trend;
    nwstate.skip_fast = dir && dir == faster_side;
    nwstate.spot = minfo.leverage == 0;
    double e;
    if (nwstate.ema_history.empty()) {
        e = tradePrice;
        loc.fut_profit+=state.total_loss;
    } else {
        e = calc_ema(state.ema_history.front(), tradePrice, cfg->ema_period);
    }
    nwstate.ema_history.push_front(e);
    while (nwstate.ema_history.size() > cfg->ema_compare_history+1) nwstate.ema_history.pop_back();
    
    if (cfg->reinvest) nwstate.budget += loc.fut_profit;
    return {
        {loc.fut_profit,0, nwstate.ema_history.back()}, new Strategy_Trending(cfg, std::move(nwstate))
    };

}
json::Value Strategy_Trending::exportState() const{
    json::Array q;
    for (double v: state.ema_history) q.push_back(v);
    return json::Object({
        {"tm",state.last_calc_time},
        {"eh",q},
        {"tl", state.total_loss},
        {"ltp", state.last_trade_price},
        {"b",state.budget},
        {"sp",state.loss_position},
        {"p",state.position},
        {"pt",state.previous_trend},
        {"skf",state.skip_fast},
        {"spot", state.spot},
    });
}
json::Value Strategy_Trending::dumpStatePretty(const IStockApi::MarketInfo &minfo) const{
    double trend = get_trend(state.last_trade_price);
    double n = state.budget*cfg->base_investment_percent/std::min(state.last_trade_price, state.last_trade_price);
    return json::Object({
          {"Total loss", state.total_loss},
          {"Position.Trend", n * trend},
          {"Budget",state.budget},
          {"Position",state.position},
          {"Trend",get_trend()},
          {"Position.Recover", state.loss_position},
          {"Ema", state.ema_history.empty()?0:state.ema_history.front()},
          {"Ema compare", state.ema_history.empty()?0:state.ema_history.back()},
          {"Skip fast", state.skip_fast}
      });
}
PStrategy Strategy_Trending::importState(json::Value src, const IStockApi::MarketInfo &minfo) const{
    const auto q = src["eh"];
    State st;
    for (auto v: q) st.ema_history.push_back(v.getNumber());
    st.last_calc_time = src["tm"].getUIntLong();
    st.total_loss = src["tl"].getNumber();
    st.last_trade_price = src["ltp"].getNumber();
    st.budget = src["b"].getNumber();
    st.position = src["p"].getNumber();
    st.loss_position = src["sp"].getNumber();
    st.previous_trend = src["pt"].getNumber();
    st.skip_fast = src["skf"].getBool();
    st.spot = src["spot"].getBool();
    return new Strategy_Trending(cfg,std::move(st));
}
Strategy_Trending::OrderData Strategy_Trending::getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const{
    LocationInfo linfo = getLocationInfo(new_price);
    double newpos = linfo.new_trend_pos + linfo.new_rev_pos;
    double diff = newpos - assets;
    double p = new_price;
    auto alert = Alert::forced;
    if (cfg->fast && diff * dir < 0 && !rej && !state.skip_fast) {
        p = cur_price;
        linfo = getLocationInfo(cur_price);;
        newpos = linfo.new_trend_pos + linfo.new_rev_pos;
        diff = newpos - assets;
        alert = Alert::disabled;
        faster_side = dir;
    }
    if (diff * dir < 0) diff = 0;
    return {p, diff, alert};
}
Strategy_Trending::MinMax Strategy_Trending::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const{
    if (state.position) {
        double tp = state.last_trade_price - (state.budget-2*state.total_loss) / state.position;
        return { state.position > 0? tp: 0.0, state.position < 0?tp:std::numeric_limits<double>::infinity()};
    }
    return { 0.0, std::numeric_limits<double>::infinity()};

}
double Strategy_Trending::getEquilibrium(double assets) const{
    return state.last_trade_price;
}
PStrategy Strategy_Trending::reset() const{
    return new Strategy_Trending(cfg, {});
}
std::string_view Strategy_Trending::getID() const{
    return id;
}
double Strategy_Trending::calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const{
    return 0;
}
Strategy_Trending::BudgetInfo Strategy_Trending::getBudgetInfo() const{
    return {state.budget, state.position};
}
double Strategy_Trending::calcCurrencyAllocation(double price, bool leveraged) const {
    return state.budget - state.total_loss;
}
Strategy_Trending::ChartPoint Strategy_Trending::calcChart(double price) const{
    return {false,0,0};
}
double Strategy_Trending::getCenterPrice(double lastPrice, double assets) const{
    return lastPrice;
}

PStrategy Strategy_Trending::init_strategy(bool leverage, double price, double assets, double currency, std::uint64_t time) const {
    double b = leverage? currency: currency + assets*price;
    if (b <= 0) throw std::runtime_error("Can't initialize strategy: no budget");
    State st;
    st.budget = b;
    st.last_trade_price = price;
    st.position = assets;
    st.total_loss = 0.01 * b;
    st.last_calc_time = time;
    st.spot = !leverage;
    return new Strategy_Trending(cfg, std::move(st));
}

Strategy_Trending::LocationInfo Strategy_Trending::getLocationInfo(double price) const {    
    double trend = get_trend(price);
    double n = state.budget*cfg->base_investment_percent/std::min(state.last_trade_price, price);    
    double pos = n * trend;
    double fut_profit = (price - state.last_trade_price) * pos;
    double profit = (price - state.last_trade_price) * state.position;
    double rev_profit = profit - fut_profit;
    double new_loss = std::max(0.0,state.total_loss - rev_profit);
    double limit_loss = state.budget * cfg->limit_loss_percent;
    if (limit_loss < new_loss && fut_profit > 0) {
        new_loss = std::max(0.0,new_loss - fut_profit);;     //stop benchmark
        fut_profit = 0;        
    }
    double new_rev_pos_abs = std::min(limit_loss,new_loss) * cfg->reversal_power / state.last_trade_price;
    double dir = sgn(price - state.last_trade_price);
    double new_rev_pos;
    double seldir = sgn(state.position);
    if (seldir == 0) seldir = -dir;
    switch (cfg->rev_str) {
        case ReversalStrategy::reduce: 
            new_rev_pos = new_rev_pos_abs * seldir; break;
        case ReversalStrategy::reverse_always:
            new_rev_pos = -new_rev_pos_abs * dir; break;
        default:
            if (trend * dir >= 0) {
                new_rev_pos = new_rev_pos_abs * seldir;
            } else {
                new_rev_pos = trend*new_rev_pos_abs;
            } 
            break;
        }

    if (state.spot && new_rev_pos < 0) new_rev_pos = 0;
    return {trend, new_loss, fut_profit, pos, new_rev_pos};
}

