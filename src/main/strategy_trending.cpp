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

double Strategy_Trending::get_trend(double price, double compare) const {
    double zone = std::abs(cfg->histersis_percent * price);
    double diff = price - compare;
    if (std::abs(diff) < zone) return cfg->histersis_percent < 0?0:state.previous_trend;
    else return sgn(diff);
}

double Strategy_Trending::get_trend() const {
    if (state.ema_history.size() < cfg->ema_compare_history) return 0;
    return get_trend(state.ema_history.front(), state.ema_history.back());
}

double Strategy_Trending::get_trend(double price) const {
    if (state.ema_history.size() < 2 || state.ema_history.size() < cfg->ema_compare_history) return 0;
    auto iter = state.ema_history.rbegin();
    ++iter;
    return get_trend(price, *iter);
   
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

    int dir = sgn(tradeSize);

    auto loc = getLocationInfo(tradePrice,dir);
    State nwstate = state;


    nwstate.last_trade_price = tradePrice;
    nwstate.total_loss = loc.new_loss;
    nwstate.calc_loss = loc.new_calc_loss;
    nwstate.position = assetsLeft;
    nwstate.loss_position = loc.new_rev_pos;    
    nwstate.previous_trend = loc.trend;
    nwstate.spot = minfo.leverage == 0; 
    nwstate.stoploss = loc.stoploss;
    double e;
        if (nwstate.ema_history.empty()) {
            e = tradePrice;
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
        {"eh",q},
        {"tl", state.total_loss},
        {"ltp", state.last_trade_price},
        {"b",state.budget},
        {"sp",state.loss_position},
        {"p",state.position},
        {"pt",state.previous_trend},
        {"spot", state.spot},
        {"cl",state.calc_loss},
        {"sl", state.stoploss},
    });
}
json::Value Strategy_Trending::dumpStatePretty(const IStockApi::MarketInfo &minfo) const{
    double trend = get_trend(state.last_trade_price);
    double n = state.budget*cfg->base_investment_percent/std::min(state.last_trade_price, state.last_trade_price);
    return json::Object({
          {"Total loss", state.total_loss},
          {"Total loss (calc)", state.calc_loss},
          {"Position.Trend", n * trend},
          {"Budget",state.budget},
          {"Position",state.position},
          {"Trend",get_trend()},
          {"Position.Recover", state.loss_position},
          {"Ema", state.ema_history.empty()?0:state.ema_history.front()},
          {"Ema compare", state.ema_history.empty()?0:state.ema_history.back()},
      });
}
PStrategy Strategy_Trending::importState(json::Value src, const IStockApi::MarketInfo &minfo) const{
    const auto q = src["eh"];
    State st;
    for (auto v: q) st.ema_history.push_back(v.getNumber());
    st.total_loss = src["tl"].getNumber();
    st.last_trade_price = src["ltp"].getNumber();
    st.budget = src["b"].getNumber();
    st.position = src["p"].getNumber();
    st.loss_position = src["sp"].getNumber();
    st.previous_trend = src["pt"].getNumber();
    st.spot = src["spot"].getBool();
    st.stoploss = src["sl"].getBool();
    st.calc_loss = src["cl"].getNumber();
    return new Strategy_Trending(cfg,std::move(st));
}
Strategy_Trending::OrderData Strategy_Trending::getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const{

    if (state.stoploss && assets * dir < 0) {
        return {cur_price, -assets, Alert::stoploss};
    }

    auto loc_new = getLocationInfo(new_price,  dir);

    if (cfg->rev_str == ReversalStrategy::two_step_reverse && (std::abs(assets) > minfo.calcMinSize(new_price))  && assets * dir < 0) {
        return {new_price, -assets, Alert::stoploss};
    }


    double new_pos = loc_new.new_pos;
    double order = new_pos-assets;
    double dirchk = order*dir;

    if (dirchk < 0) {
        order = 0;
    }

    return {new_price, order, Alert::stoploss};

}
Strategy_Trending::MinMax Strategy_Trending::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const{
    if (state.position) {
        double tp = state.last_trade_price - (currencies + (minfo.leverage?0:assets*state.last_trade_price)) / state.position;
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
    st.spot = !leverage;
    return new Strategy_Trending(cfg, std::move(st));
}

Strategy_Trending::LocationInfo Strategy_Trending::getLocationInfo(double price, int orddir) const {    
    double trend = get_trend(price);
    double n = state.budget*cfg->base_investment_percent/std::min(state.last_trade_price, price);    
    double pos = n * trend;    
    double fut_profit = (price - state.last_trade_price) * pos;
    if (state.spot && pos < 0) 
        fut_profit = 0;
    double profit = (price - state.last_trade_price) * state.position;
    double rev_profit = profit - fut_profit;
    double new_loss = std::max(0.0,state.total_loss - rev_profit);    
    double limit_loss = state.budget * cfg->limit_loss_percent;
    double min_loss = state.budget * cfg->min_loss_percent;
    bool stoploss = false;
    double calc_loss = std::min({new_loss, limit_loss, std::max(0.0,state.calc_loss - rev_profit* (profit<0?2:1))});

    if (limit_loss < new_loss) {
        if (fut_profit > 0) {
            new_loss = std::max(0.0,new_loss - fut_profit);     //stop benchmark        
            fut_profit = 0;  
        }
    } 
    if (calc_loss < min_loss) calc_loss = min_loss;
    double new_rev_pos_abs = calc_loss * cfg->reversal_power / price;
    int dir = orddir?orddir:static_cast<int>(sgn(state.last_trade_price - price));
    double new_rev_pos;
    double new_pos;
    double seldir = sgn(state.position);
    if (seldir == 0) seldir = dir;
    switch (cfg->rev_str) {
        case ReversalStrategy::reduce: 
            new_rev_pos = new_rev_pos_abs * seldir; 
            new_pos = new_rev_pos + pos;
            break;
        case ReversalStrategy::reduce2: 
            new_rev_pos = new_rev_pos_abs * seldir; 
            if (dir * pos < 0 && (new_rev_pos + pos - state.position) * dir < 0) {
                pos = -pos;
            }
            new_pos = new_rev_pos + pos;
            break;
        case ReversalStrategy::two_step_reverse:
        case ReversalStrategy::reverse_always:
            new_rev_pos = new_rev_pos_abs * dir; 
            new_pos = new_rev_pos + pos;
            break;
            
        default:
            if (trend == 0) {
                new_rev_pos = new_rev_pos_abs * seldir;
                new_pos = new_rev_pos + pos;
            } else {                
                new_rev_pos = trend*new_rev_pos_abs;            
                new_pos = new_rev_pos + pos;
                if ((new_pos * state.position < 0)   //reverse position
                    && (new_pos * dir < 0))  {//can't reverse now!
                        if (trend == state.previous_trend) { //thend no longer changing
                            if (cfg->rev_str == ReversalStrategy::reverse_by_trend_fast_zero) { //for this strategy
                                stoploss = true; //mark - we need stoploss
                            }
                        } else {
                            new_pos = -new_pos; //keep current dir;
                        }
                    }
                
            }

            break;
        }

    return {trend, new_loss, calc_loss, fut_profit, 
        pos, new_rev_pos, new_pos, stoploss};
}

