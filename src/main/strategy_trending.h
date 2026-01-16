#pragma once
#include "istrategy.h"
#include <imtjson/value.h>
#include <deque>

class Strategy_Trending: public IStrategy {
public:

    enum class ReversalStrategy {
        reduce = 0,
        reverse_by_trend = 1,
        reverse_by_trend_fast = 2,
        reverse_always = 3
    };


    struct Config {
        unsigned int ema_period;
        unsigned int ema_compare_history;
        double base_investment_percent;
        double reversal_power;
        double limit_loss_percent;
        double histersis_percent;
        bool reinvest;
        ReversalStrategy rev_str;
    };

    struct State {
        std::uint64_t last_calc_time = 0;
        std::deque<double> ema_history = {};
        double total_loss = 0;
        double last_trade_price = 0;
        double budget = 0;
        double position = 0;
        double loss_position = 0;
        double previous_trend = 0;
        bool spot = false;
        int fast_reverse_to = 0;
        bool was_fast_reverse = false;
        


    };
    static constexpr auto id = std::string_view("trending");

    Strategy_Trending(const Config &cfg);
    Strategy_Trending(const std::shared_ptr<const Config > &cfg, State &&state);

    virtual ~Strategy_Trending() override = default;

	virtual bool isValid() const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult, PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;
	virtual json::Value exportState() const override;
	virtual json::Value dumpStatePretty(const IStockApi::MarketInfo &minfo) const override;
	virtual PStrategy importState(json::Value src, const IStockApi::MarketInfo &minfo) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double cur_price, double new_price, double dir, double assets, double currency, bool rej) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium(double assets) const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;
	virtual double calcInitialPosition(const IStockApi::MarketInfo &minfo, double price, double assets, double currency) const override;
	virtual BudgetInfo getBudgetInfo() const override;
	virtual double calcCurrencyAllocation(double price, bool leveraged) const override;	
	virtual ChartPoint calcChart(double price) const override;
	virtual double getCenterPrice(double lastPrice, double assets) const override;
protected:

    std::shared_ptr<const Config> cfg;
    State state;

    PStrategy init_strategy(bool leverage, double price, double assets, double currency, std::uint64_t time) const;
    static double calc_ema(double prev_ema, double cur_value, int interval);
    double get_trend() const;
    double get_trend(double price) const;

    struct LocationInfo {
        double trend;
        double new_loss;
        double fut_profit;
        double new_trend_pos;
        double new_rev_pos;
    };

    LocationInfo getLocationInfo(double price) const;

};
