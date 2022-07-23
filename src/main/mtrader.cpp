#include <imtjson/value.h>
#include <imtjson/string.h>
#include <imtjson/binary.h>
#include "istockapi.h"
#include "mtrader.h"
#include "strategy.h"

#include <chrono>
#include <cmath>
#include <shared/logOutput.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
#include <numeric>
#include <queue>
#include <random>

#include "../shared/stringview.h"
#include "papertrading.h"

#include "emulatedLeverageBroker.h"
#include "ibrokercontrol.h"
#include "sgn.h"
#include "swap_broker.h"

using ondra_shared::logDebug;
using ondra_shared::logError;
using ondra_shared::logInfo;
using ondra_shared::logNote;
using ondra_shared::logProgress;
using ondra_shared::logWarning;
using ondra_shared::StringView;
using ondra_shared::StrViewA;

json::NamedEnum<Dynmult_mode> strDynmult_mode  ({
	{Dynmult_mode::disabled, "disabled"},
	{Dynmult_mode::independent, "independent"},
	{Dynmult_mode::together, "together"},
	{Dynmult_mode::alternate, "alternate"},
	{Dynmult_mode::half_alternate, "half_alternate"}
});




void MTrader_Config::loadConfig(json::Value data) {
	pairsymb = data["pair_symbol"].getString();
	broker = data["broker"].getString();
	title = data["title"].getString();

	auto strdata = data["strategy"];
	auto strstr = strdata["type"].toString();
	strategy = Strategy::create(strstr.str(), strdata);

	auto swp = data["swap_symbols"];
	if (swp.type() == json::boolean) {
		swap_mode = swp.getBool()?SwapMode::invert:SwapMode::no_swap;
	} else {
		swap_mode = static_cast<SwapMode>(data["swap_symbols"].getUInt());
	}


	buy_step_mult = data["buy_step_mult"].getValueOrDefault(1.0);
	sell_step_mult = data["sell_step_mult"].getValueOrDefault(1.0);
	min_size = data["min_size"].getValueOrDefault(0.0);
	max_size = data["max_size"].getValueOrDefault(0.0);
	json::Value min_balance = data["min_balance"];
	json::Value max_balance = data["max_balance"];
	json::Value max_costs = data["max_costs"];
	if (min_balance.type() == json::number) this->min_balance = min_balance.getNumber();
	if (max_balance.type() == json::number) this->max_balance = max_balance.getNumber();
	if (max_costs.type() == json::number) this->max_costs = max_costs.getNumber();

	dynmult_raise = data["dynmult_raise"].getValueOrDefault(0.0);
	dynmult_fall = data["dynmult_fall"].getValueOrDefault(1.0);
	dynmult_cap = data["dynmult_cap"].getValueOrDefault(100.0);
	dynmult_mode = strDynmult_mode[data["dynmult_mode"].getValueOrDefault("half_alternate")];

	accept_loss = data["accept_loss"].getValueOrDefault(1);
	adj_timeout = std::max<unsigned int>(5,data["adj_timeout"].getUInt());
	max_leverage = data["max_leverage"].getValueOrDefault(0.0);

	force_spread = data["force_spread"].getValueOrDefault(0.0);
	report_order = data["report_order"].getValueOrDefault(0.0);
	secondary_order_distance = data["secondary_order"].getValueOrDefault(0.0)*0.01;
	grant_trade_minutes = static_cast<unsigned int>(data["grant_trade_hours"].getValueOrDefault(0.0)*60);

	spread_calc_sma_hours = data["spread_calc_sma_hours"].getValueOrDefault(24.0);
	spread_calc_stdev_hours = data["spread_calc_stdev_hours"].getValueOrDefault(4.0);

	if (data["dry_run"].getBool() == true) throw std::runtime_error("Paper trading option is no longer supported");
	paper_trading = data["paper_trading"].getValueOrDefault(false);
	dont_allocate = data["dont_allocate"].getValueOrDefault(false) ;
	enabled= data["enabled"].getValueOrDefault(true);
	hidden = data["hidden"].getValueOrDefault(false);
	dynmult_sliding = data["dynmult_sliding"].getValueOrDefault(false);
	dynmult_mult = data["dynmult_mult"].getValueOrDefault(false);
	emulate_leveraged=data["emulate_leveraged"].getValueOrDefault(0.0);
	reduce_on_leverage=data["reduce_on_leverage"].getBool();
	freeze_spread=data["spread_freeze"].getBool();
	trade_within_budget = data["trade_within_budget"].getBool();
	init_open = data["init_open"].getNumber();

	if (paper_trading) {
		paper_trading_src_state = data["pp_source"].getString();
	}

	if (dynmult_raise > 1e6) throw std::runtime_error("'dynmult_raise' is too big");
	if (dynmult_raise < 0) throw std::runtime_error("'dynmult_raise' is too small");
	if (dynmult_fall > 100) throw std::runtime_error("'dynmult_fall' must be below 100");
	if (dynmult_fall <= 0) throw std::runtime_error("'dynmult_fall' must not be negative or zero");

}

MTrader::MTrader(IStockSelector &stock_selector,
		StoragePtr &&storage,
		PStatSvc &&statsvc,
		const WalletCfg &walletCfg,
		Config config)
:stock(selectStock(stock_selector,config.broker, config.swap_mode, config.emulate_leveraged, config.paper_trading))
,cfg(config)
,storage(std::move(storage))
,statsvc(std::move(statsvc))
,wcfg(walletCfg)
,strategy(config.strategy)
,dynmult({cfg.dynmult_raise,cfg.dynmult_fall, cfg.dynmult_cap, cfg.dynmult_mode, cfg.dynmult_mult})
,acb_state(0,0)
,spread_fn(defaultSpreadFunction(cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours, cfg.force_spread))
{
	magic = this->statsvc->getHash() & 0xFFFFFFFF;
	magic2 = (~magic) & 0xFFFFFFFF;; //magic number for secondary orders
	std::random_device rnd;
	uid = 0;
	while (!uid) {
		uid = rnd();
	}

	if (cfg.dont_allocate) {
		//create independed wallet db
		wcfg.walletDB = wcfg.walletDB.make();
	}

}


bool MTrader::Order::isSimilarTo(const IStockApi::Order& other, double step, bool inverted) {
	double p1,p2;
	if (inverted) {
		 p1 = 1.0/price;
		 p2 = 1.0/other.price;
	} else {
		p1 = price;
		p2 = other.price;
	}
	return std::fabs(p1 - p2) < step && size * other.size > 0;
}


PStockApi MTrader::selectStock(IStockSelector &stock_selector, std::string_view broker_name, SwapMode swap_mode, int emulate_leverage, bool paper_trading) {
	PStockApi s = stock_selector.getStock(broker_name);
	if (s == nullptr) throw std::runtime_error(std::string("Unknown broker name: ")+std::string(broker_name));
	switch (swap_mode) {
	case SwapMode::invert: s = std::make_shared<InvertBroker>(s);break;
	case SwapMode::swap: s = std::make_shared<SwapBroker>(s);break;
	default: break;
	}
	if (emulate_leverage>0) {
		s = std::make_shared<EmulatedLeverageBroker>(s,emulate_leverage);
	}
	if (paper_trading) {
		auto new_s = std::make_shared<PaperTrading>(s);
		return new_s;
	} else {
		return s;
	}
}

double MTrader::raise_fall(double v, bool raise) const {
	if (raise) {
		double rr = cfg.dynmult_raise/100.0;
		return v + rr;
	} else {
		double ff = cfg.dynmult_fall/100.0;
		return std::max(1.0,v - ff);
	}
}

bool MTrader::need_init() const {
	return need_load;
}

void MTrader::init() {
	if (need_load){
		initialize();
		loadState();
		need_load = false;
	}
}

const MTrader::TradeHistory& MTrader::getTrades() const {
	return trades;
}

bool MTrader::flushExpPos() {
    if (expPos.partial.getPos()) {
        strategy.onTrade(minfo, expPos.partial.getOpen(), expPos.partial.getPos(), position, currency);
        expPos.partial = ACB(0,0,0);
        expPos.norm_accum = 0;
        expPos.norm_profit = 0;
        return true;
    } else {
        return false;
    }
}
void MTrader::alertTrigger(const Status &st, double price, int dir, AlertReason reason) {

    flushExpPos();

	IStockApi::Trade tr{
		json::Value(json::String({"ALERT:",json::Value(st.chartItem.time).toString()})),
		st.chartItem.time,
		0,
		price,
		0,
		price
	};

	double last_np = 0;
	double last_ap = 0;
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;
	}


	if (!achieve_mode) {
		auto norm = strategy.onTrade(minfo, price, 0, position, st.currencyBalance);
		tr.eff_size-=norm.normAccum;
		accumulated +=norm.normAccum;
		position -=norm.normAccum;
		trades.push_back(TWBItem(tr, last_np+=norm.normProfit, last_ap+=norm.normAccum, norm.neutralPrice, false, static_cast<char>(dir), static_cast<char>(reason)));
	} else {
		trades.push_back(TWBItem(tr, last_np, last_ap, 0, true, static_cast<char>(dir), static_cast<char>(reason)));
	}
	refresh_minfo = true;
}

void MTrader::dorovnani(Status &st, double assetBalance, double price) {
	double diff = st.assetBalance - assetBalance-getAccumulated();
	st.new_trades.push_back(IStockApi::Trade{
		json::Value(json::String({"ADJ:",json::Value(st.chartItem.time).toString()})),
		st.chartItem.time,
		diff,
		price,
		diff,
		price
	});
	lastTradePrice = price;
	refresh_minfo = true;
}

void MTrader::perform(bool manually) {


    double buy_norm = 0;
    double sell_norm = 0;
    int last_trade_dir = 0;

	try {
		init();

		if (refresh_minfo) {
			try {
				update_minfo();
				refresh_minfo = false;
			} catch (std::exception &e) {
				logWarning("Failed to refresh market info: $1", e.what());
			}
		}

		//get current status
		auto status = getMarketStatus();
		//load orders to order manager
		orderMgr.load(std::move(status.openOrders));
		//update currency from status
		currency = status.currencyBalance; //TODO check leaked trade

		//if we have brokerCurrencyBalance
		if (status.brokerCurrencyBalance.has_value()) {
		    //update balance cache
			wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.currency_symbol, *status.brokerCurrencyBalance);
		}
		//if not leverage, we must update allocated assets
		if (minfo.leverage == 0 && position_valid) {
//			double accum = getAccumulated(); //TODO add accumulation
		    //update balance cache
			if (status.brokerAssetBalance.has_value()) wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.asset_symbol, *status.brokerAssetBalance);
			//reserve position
			wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position);
			//wcfg.accumDB.lock()->alloc(getWalletAssetKey(), accum);
		}

		//process reported trades
		processTrades(status);

		//process alerts
		orderMgr.listAlerts(status.curPrice, [&](const AlertInfo &alrt){
		    double dir = sgn(alrt.price - status.curPrice);
		    alertTrigger(status, alrt.price, dir, alrt.reason);
            update_dynmult(dir>0,dir<0);

		});

		orderMgr.initAlerts(status.curPrice);

		//we need alerts, if there the last trade was alert
		bool need_alerts = trades.empty()?false:trades.back().size == 0;
		//retrieve last trade size
		//double lastTradeSize = trades.empty()?0:trades.back().eff_size;

		//retrieve last trade prices
		if (!lastTradePrice) {
			lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium(status.assetBalance):status.curPrice;
			if (!std::isfinite(lastTradePrice)) lastTradePrice = status.curPrice;		}

		//prepare center price
		double centerPrice = need_alerts?lastTradePrice:strategy.getCenterPrice(lastTradePrice, position - expPos.partial.getPos());

		//depend on mode calculate center price
		if (cfg.dynmult_sliding) {
			centerPrice = (centerPrice-lastTradePrice)+lastPriceOffset+status.spreadCenter;
		}



		//TODO: trade now + achieve mode

		//call on idle of the strategy
        strategy.onIdle(minfo, status.ticker, position, currency);

        if (!cfg.enabled) {
            if (!cfg.hidden) {
                statsvc->reportError(IStatSvc::ErrorObj("Automatic trading is disabled"));
            }
        } else if (need_initial_reset) {
            if (!cfg.hidden) {
                statsvc->reportError(IStatSvc::ErrorObj("Reset required"));
            }
        } else if (status.curStep == 0) {
            if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj("Initializing, please wait\n(5 minutes aprox.)"));
        } else {

            double lspread = status.curStep*cfg.buy_step_mult;
            double hspread = status.curStep*cfg.sell_step_mult;
            if (cfg.freeze_spread) {
                if (frozen_spread_side<0) {
                    lspread = std::min(frozen_spread, lspread);
                } else if (frozen_spread_side>0) {
                    hspread = std::min(frozen_spread, hspread);
                }
            }

            double buyBase;
            double sellBase;
            {
                auto buyTick = minfo.priceToTick((status.ticker.bid+status.ticker.ask)*0.5);
                auto sellTick = buyTick;
                auto bidTick = minfo.priceToTick(status.ticker.bid);
                auto askTick = minfo.priceToTick(status.ticker.ask);
                while (buyTick >= askTick) buyTick--;
                while (sellTick <= bidTick) sellTick++;
                buyBase = minfo.tickToPrice(buyTick);
                sellBase = minfo.tickToPrice(sellTick);
            }


            double adjpos = position - expPos.partial.getPos();

            hspread = std::max(hspread,1e-10); //spread can't be zero, so put there small number
            lspread = std::max(lspread,1e-10);
                //calculate buy order
            auto buyorder = calculateOrder(strategy,centerPrice,-lspread,
                    dynmult.getBuyMult(),buyBase,
                    adjpos,status.currencyBalance,need_alerts);
                //calculate sell order
            auto sellorder = calculateOrder(strategy,centerPrice,hspread,
                    dynmult.getSellMult(),sellBase,
                    adjpos,status.currencyBalance,need_alerts);


            if (buyorder.size - expPos.partial.getPos() < minfo.calcMinOrderSize(buyorder.price)) {
                buyorder.size = 0;
                if (flushExpPos()) {
                    buyorder.alert = IStrategy::Alert::disabled;
                    last_trade_dir = 1;
                    update_dynmult(true, false);
                }
            } else {
                buyorder.size -= expPos.partial.getPos();
            }

            if (sellorder.size - expPos.partial.getPos() > -minfo.calcMinOrderSize(sellorder.price)) {
                sellorder.size = 0;
                if (flushExpPos()) {
                    sellorder.alert = IStrategy::Alert::disabled;
                    last_trade_dir = -1;
                    update_dynmult(false, true);
                }
            } else {
                sellorder.size -= expPos.partial.getPos();
            }

            orderMgr.placeOrder(buyorder, 1);
            orderMgr.placeOrder(sellorder, -1);


            Strategy buy_state = strategy;
            Strategy sell_state = strategy;

            auto buy_norm_info = buy_state.onTrade(minfo, buyorder.price, buyorder.size, position+buyorder.size, currency);
            auto sell_norm_info = sell_state.onTrade(minfo, sellorder.price, sellorder.size, position+sellorder.size, currency);
            buy_norm = buy_norm_info.normProfit;
            sell_norm = sell_norm_info.normProfit;

            if (cfg.secondary_order_distance>0) {
                auto buyorder2 = calculateOrder(buy_state,buyorder.price,-status.curStep,
                                                cfg.secondary_order_distance,
                                                status.ticker.bid,position+buyorder.size,
                                                currency,false);
                auto sellorder2 = calculateOrder(sell_state,sellorder.price,status.curStep,
                                                cfg.secondary_order_distance,
                                                status.ticker.bid,position+sellorder.size,
                                                currency,false);
                orderMgr.placeOrder(buyorder2, 2);
                orderMgr.placeOrder(sellorder2, -2);
            }

        }

        orderMgr.commit(*stock, minfo, cfg.pairsymb, tradingInstance);

        if (!cfg.hidden) {

            auto buy1 = orderMgr.findPlacedOrderById(1);
            auto sell1 = orderMgr.findPlacedOrderById(-1);
            auto buy2 = orderMgr.findPlacedOrderById(2);
            auto sell2 = orderMgr.findPlacedOrderById(-2);


            statsvc->reportOrders(1, OrderPlaceResult::getOrder(buy1), OrderPlaceResult::getOrder(sell1));
            statsvc->reportOrders(2, OrderPlaceResult::getOrder(buy2), OrderPlaceResult::getOrder(sell2));
            statsvc->reportError(IStatSvc::ErrorObj(OrderPlaceResult::getError(buy1),OrderPlaceResult::getError(sell1)));

            //report trades to UI
            statsvc->reportTrades(position, trades);
            //report price to UI
            statsvc->reportPrice(status.curPrice);
            //report misc
            //report misc
            auto minmax = strategy.calcSafeRange(minfo,status.assetAvailBalance,status.currencyAvailBalance);
            auto budget = strategy.getBudgetInfo();
            std::optional<double> budget_extra;
            if (!trades.empty())
            {
//              double last_price = trades.back().eff_price;
                double locked = wcfg.walletDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid)).otherTraders;
                double currency = strategy.calcCurrencyAllocation(status.curPrice, minfo.leverage>0);
                budget_extra =  status.currencyUnadjustedBalance - locked - currency;
            }

            statsvc->reportMisc(IStatSvc::MiscData{
                last_trade_dir,
                achieve_mode,
                cfg.enabled,
                strategy.getEquilibrium(position),
                status.curStep*0.5*(cfg.buy_step_mult+cfg.sell_step_mult),
                dynmult.getBuyMult(),
                dynmult.getSellMult(),
                minmax.min,
                minmax.max,
                budget.total,
                budget.assets,
                accumulated,
                budget_extra,
                trades.size(),
                trades.empty()?0:(trades.back().time-trades[0].time),
                centerPrice,
                position,
                buy_norm,
                sell_norm,
                acb_state.getOpen(),
                acb_state.getRPnL(),
                acb_state.getUPnL(status.curPrice)
            });
        }


        if (!manually) {
            if (chart.empty() || chart.back().time < status.chartItem.time) {
                //store current price (to build chart)
                chart.push_back(status.chartItem);
                {
                    //delete very old data from chart
                    unsigned int max_count = std::max<unsigned int>(std::max(cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours),240*60);
                    if (chart.size() > max_count)
                        chart.erase(chart.begin(),chart.end()-max_count);
                }
            }
        }




        //save state
        saveState();
        first_cycle = false;

    } catch (std::exception &e) {
        if (!cfg.hidden) {
            statsvc->reportTrades(position,trades);
            std::string error;
            error.append(e.what());
            statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
            statsvc->reportMisc(IStatSvc::MiscData{
                0,false,cfg.enabled,0,0,dynmult.getBuyMult(),dynmult.getSellMult(),0,0,0,0,accumulated,0,
                trades.size(),trades.empty()?0UL:(trades.back().time-trades[0].time),lastTradePrice,position,
                        0,0,acb_state.getOpen(),acb_state.getRPnL(),acb_state.getUPnL(lastTradePrice)
            },true);
            statsvc->reportPrice(trades.empty()?1:trades.back().price);
            throw;
        }
    }


#if 0


		if (recalc) {
            double lp = status.curPrice;
            int fst = 0;
            if (!trades.empty()) {
                const auto &tb = trades.back();
                lp = tb.eff_price;
                if (tb.size<0) {
                    fst = -1;
                } else if (tb.size > 0) {
                    fst = 1;
                } else {
                    fst = frozen_spread_side;
                }
                recalc = fast_trade || std::abs(lp-eq)/eq < 0.001;
                if (!recalc) {
                    auto o = strategy.getNewOrder(minfo, lp, lp, sgn(lastTradeSize), status.assetBalance, status.curPrice, false);
                    double minsz = std::max(minfo.min_size,std::max(minfo.asset_step, minfo.min_volume/lp));
                    auto sz = std::abs(o.size);
                    if (sz < minsz) recalc = true;
                }
            }
            if (recalc) {
                update_dynmult(lastTradeSize > 0, lastTradeSize < 0);
                lastTradePrice = lp;
                lastPriceOffset = lastTradePrice - status.spreadCenter;
                frozen_spread = status.curStep;
                frozen_spread_side = fst;
            }
        }

        if (grant_trade) {
            dynmult.reset();
        }
*/


        if (status.curStep) {

            if (!cfg.enabled || need_initial_reset || delayed_trade_detect)  {
                if (!cfg.hidden) {
                    if (delayed_trade_detect) {
                        if (adj_wait>1) {
                            statsvc->reportError(IStatSvc::ErrorObj("Trade detected, waiting for confirmation (ADJ Timeout)"));
                        }
                    } else if (!cfg.enabled) {
                        statsvc->reportError(IStatSvc::ErrorObj("Automatic trading is disabled"));
                    } else {
                        statsvc->reportError(IStatSvc::ErrorObj("Reset required"));
                    }
                }
            } else {
                double maxPosition;
                if (need_alerts && checkReduceOnLeverage(status, maxPosition)) {
                    double diff = maxPosition - status.assetBalance;
                    if (diff < 0) {
                        sellorder = Order(diff, status.ticker.ask, IStrategy::Alert::disabled, AlertReason::unknown);
                        buyorder = Order(0, status.ticker.ask/2, IStrategy::Alert::disabled, AlertReason::unknown);
                    } else {
                        buyorder = Order(diff, status.ticker.bid, IStrategy::Alert::disabled, AlertReason::unknown);
                        sellorder = Order(0, status.ticker.bid*2, IStrategy::Alert::disabled, AlertReason::unknown);
                    }
                } else {
                    double lspread = status.curStep*cfg.buy_step_mult;
                    double hspread = status.curStep*cfg.sell_step_mult;
                    if (cfg.freeze_spread) {
                        if (frozen_spread_side<0) {
                            lspread = std::min(frozen_spread, lspread);
                        } else if (frozen_spread_side>0) {
                            hspread = std::min(frozen_spread, hspread);
                        }
                    }

                    if (grant_trade) {
                        lspread = hspread = 0;
                        need_alerts = false;
                    }

                    double buyBase;
                    double sellBase;
                    {
                        auto buyTick = minfo.priceToTick((status.ticker.bid+status.ticker.ask)*0.5);
                        auto sellTick = buyTick;
                        auto bidTick = minfo.priceToTick(status.ticker.bid);
                        auto askTick = minfo.priceToTick(status.ticker.ask);
//							logDebug("TICKS: buyTick=$1, sellTick=$2, bidTick=$3, askTick=$4, step=$5", buyTick, sellTick, bidTick, askTick, minfo.currency_step);
                        while (buyTick >= askTick) buyTick--;
                        while (sellTick <= bidTick) sellTick++;
                        buyBase = minfo.tickToPrice(buyTick);
                        sellBase = minfo.tickToPrice(sellTick);
//							logDebug("TICKS: buyTick=$1, sellTick=$2, bidTick=$3, askTick=$4, step=$5", buyTick, sellTick, bidTick, askTick, minfo.currency_step);
//							logDebug("TICKS: buyBase=$1, sellBase=$2, 1/buyBase=$3, 1/sellBase=$4", buyBase, sellBase, 1.0/buyBase, 1.0/sellBase);
                    }



                    double adjpos = position - expPos.partial.getPos();

                    hspread = std::max(hspread,1e-10); //spread can't be zero, so put there small number
                    lspread = std::max(lspread,1e-10);
                        //calculate buy order
                    buyorder = calculateOrder(strategy,centerPrice,-lspread,
                            dynmult.getBuyMult(),buyBase,
                            position,status.currencyBalance,need_alerts);
                        //calculate sell order
                    sellorder = calculateOrder(strategy,centerPrice,hspread,
                            dynmult.getSellMult(),sellBase,
                            position,status.currencyBalance,need_alerts);

                    if (cfg.dynmult_sliding && !need_alerts) {
                        double bp = centerPrice*std::exp(-lspread);
                        double sp = centerPrice*std::exp(hspread);
                        if (status.ticker.bid > sp) {
                            auto b2 = calculateOrder(strategy,lastTradePrice,-lspread,
                                    1.0,status.ticker.bid,
                                    position,status.currencyBalance,true);
                            if (b2.size) buyorder = b2;
                        }
                        if (status.ticker.ask < bp) {
                            auto s2 = calculateOrder(strategy,lastTradePrice,hspread,
                                    1.0,status.ticker.ask,
                                    position,status.currencyBalance,true);
                            if (s2.size) sellorder = s2;

                        }
                    }

                    if (buyorder->size - expPos.partial.getPos() <= 0 ||
                        sellorder->size - expPos.partial.getPos() >= 0) {

                        strategy.onTrade(minfo, expPos.partial.getOpen(), expPos.partial.getPos(), position, status.currencyBalance);

                        expPos.hight = buyorder->size;
                        expPos.low = sellorder->size;
                        expPos.norm_accum = 0;
                        expPos.norm_profit = 0;
                        expPos.partial = ACB();
                    } else {
                        expPos.hight = buyorder->size;
                        expPos.low = sellorder->size;
                    }


                }


                if (achieve_mode) {
                    sellorder->alert = IStrategy::Alert::disabled;
                    buyorder->alert = IStrategy::Alert::disabled;
                }

                orderMgr.placeOrder(buyorder->price, buyorder->size, 1);
                orderMgr.placeOrder(sellorder->price, sellorder->size, -1);

                if (buy_alert.has_value() && sell_alert.has_value() && buy_alert->price > sell_alert->price) {
                    std::swap(buy_alert, sell_alert);
                }

                if (!recalc && !manually) {
                    update_dynmult(false,false);
                }

            }
        } else {
            if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj("Initializing, please wait\n(5 minutes aprox.)"));
        }

        recalc = false;


		Strategy buy_state = strategy;
		Strategy sell_state = strategy;
		double buy_norm = 0;
		double sell_norm = 0;
		if (!cfg.hidden || cfg.secondary_order_distance>0) {
			if (sell_alert.has_value()) {
				sell_norm = sell_state.onTrade(minfo, sell_alert->price, 0, position, status.currencyBalance).normProfit;
			} else if (sellorder.has_value()) {
				sell_norm = sell_state.onTrade(minfo, sellorder->price, sellorder->size, position+sellorder->size, status.currencyBalance).normProfit;
			}
			if (buy_alert.has_value()) {
				buy_norm = buy_state.onTrade(minfo, buy_alert->price, 0, position, status.currencyBalance).normProfit;
			} else if (buyorder.has_value()) {
				buy_norm = buy_state.onTrade(minfo, buyorder->price, buyorder->size, position+buyorder->size, status.currencyBalance).normProfit;
			}
		}


        if (cfg.secondary_order_distance > 0 && orders.buy.has_value() && orders.buy->price > 0) {
            std::optional<AlertInfo> alert;
            try {
                auto buyorder = calculateOrder(buy_state,orders.buy->price,-status.curStep,
                                                cfg.secondary_order_distance,
                                                status.ticker.bid,position+orders.buy->size,
                                                status.currencyBalance,false);
                setOrder(orders.buy2, buyorder, alert, true);
            } catch (std::exception &e) {
                logError("Failed to create secondary order: $1", e.what());
            }
        } else if (orders.buy2.has_value()) {
            stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.buy2->id, 0);
            orders.buy2.reset();
        }


        if (cfg.secondary_order_distance > 0 && orders.sell.has_value() && orders.sell->price > 0) {
            std::optional<AlertInfo> alert;
            try {
                auto sellorder = calculateOrder(sell_state,orders.sell->price, status.curStep,
                                                     cfg.secondary_order_distance,
                                                     status.ticker.ask, position+orders.sell->size,
                                                     status.currencyBalance,false);
                setOrder(orders.sell2, sellorder, alert, true);
            } catch (std::exception &e) {
                logError("Failed to create secondary order: $1", e.what());
            }
        } else  if (orders.sell2.has_value()) {
            stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.sell2->id, 0);
            orders.sell2.reset();
        }


		if (!cfg.hidden) {
			int last_trade_dir = !anytrades?0:sgn(lastTradeSize);
			if (fast_trade) {
				if (last_trade_dir < 0) orders.sell.reset();
				if (last_trade_dir > 0) orders.buy.reset();
			}

			//report orders to UI
			statsvc->reportOrders(1,orders.buy,orders.sell);
			//report trades to UI
			statsvc->reportTrades(position, trades);
			//report price to UI
			statsvc->reportPrice(status.curPrice);
			//report misc
			auto minmax = strategy.calcSafeRange(minfo,status.assetAvailBalance,status.currencyAvailBalance);
			auto budget = strategy.getBudgetInfo();
			std::optional<double> budget_extra;
			if (!trades.empty())
			{
//				double last_price = trades.back().eff_price;
				double locked = wcfg.walletDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid)).otherTraders;
				double currency = strategy.calcCurrencyAllocation(status.curPrice, minfo.leverage>0);
				budget_extra =  status.currencyUnadjustedBalance - locked - currency;
			}

			statsvc->reportMisc(IStatSvc::MiscData{
				last_trade_dir,
				achieve_mode,
				cfg.enabled,
				eq,
				status.curStep*0.5*(cfg.buy_step_mult+cfg.sell_step_mult),
				dynmult.getBuyMult(),
				dynmult.getSellMult(),
				minmax.min,
				minmax.max,
				budget.total,
				budget.assets,
				accumulated,
				budget_extra,
				trades.size(),
				trades.empty()?0:(trades.back().time-trades[0].time),
				centerPrice,
				position,
				buy_norm,
				sell_norm,
				acb_state.getOpen(),
				acb_state.getRPnL(),
				acb_state.getUPnL(status.curPrice)
			});

		}

		if (!manually) {
			if (chart.empty() || chart.back().time < status.chartItem.time) {
				//store current price (to build chart)
				chart.push_back(status.chartItem);
				{
					//delete very old data from chart
					unsigned int max_count = std::max<unsigned int>(std::max(cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours),240*60);
					if (chart.size() > max_count)
						chart.erase(chart.begin(),chart.end()-max_count);
				}
			}
		}


		if (!cfg.hidden) statsvc->reportOrders(2,orders.buy2,orders.sell2);



		//save state
		saveState();
		first_cycle = false;
#endif
}


#if 0
void MTrader::setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<AlertInfo> &alert, bool secondary) {
	alert.reset();
	try {
		if (neworder.price < 0) {
			if (orig.has_value()) return;
			throw std::runtime_error("Order rejected - negative price");
		}
		if (!std::isfinite(neworder.price)) {
			if (orig.has_value()) return;
			throw std::runtime_error("Order rejected - Price is not finite");
		}
		if (!std::isfinite(neworder.size)) {
			if (orig.has_value()) return;
			throw std::runtime_error("Order rejected - Size is not finite");
		}
		if (neworder.alert == IStrategy::Alert::forced && neworder.size == 0) {
			if (orig.has_value() && orig->id.hasValue()) {
				//cancel current order
			    orderMgr.placeOrder(price, size, custom_id)
				stock->placeOrder(cfg.pairsymb,0,0,nullptr,orig->id,0);
			}
			alert = AlertInfo{neworder.price, neworder.ar};
			neworder.size = 0;
			neworder.update(orig);
			return;
		}

		if (neworder.size == 0) {
			if (neworder.alert == IStrategy::Alert::disabled|| orig.has_value()) return;
			alert = AlertInfo{neworder.price, neworder.ar};
			neworder.update(orig);
			return;
		}

		IStockApi::Order n {json::undefined, secondary?magic2:magic, neworder.size, neworder.price};
		try {
			json::Value replaceid;
			double replaceSize = 0;
			if (orig.has_value()) {
				if (neworder.isSimilarTo(*orig, minfo.currency_step, minfo.invert_price)) return;
				replaceid = orig->id;
				replaceSize = std::fabs(orig->size);
			}
			json::Value placeid = stock->placeOrder(
						cfg.pairsymb,
						n.size,
						n.price,
						n.client_id,
						replaceid,
						replaceSize);
			if (!placeid.hasValue()) {
				alert = AlertInfo{neworder.price, neworder.ar};
				neworder.size = 0;
				neworder.update(orig);
			} else if (placeid != replaceid) {
				n.id = placeid;
				orig = n;
			} else {
				neworder.update(orig);
			}
		} catch (...) {
			orig = n;
			throw;
		}
	} catch (...) {
		throw;
	}
}
#endif

template<typename T>
static std::pair<double,double> sumTrades(const std::pair<double,double> &a, const T &b) {
	return {
		a.first + b.eff_size,
		a.second - b.eff_size*b.eff_price
	};
}


MTrader::Status MTrader::getMarketStatus() const {

	Status res;

	IStockApi::Trade ftrade = {json::Value(), 0, 0, 0, 0, 0}, *last_trade = &ftrade;

	IStockApi::TradingStatus tstatus = stock->getTradingStatus(cfg.pairsymb, tradingInstance);



	for (auto &&k : tstatus.fills) {
		if (last_trade->price == k.price) {
			last_trade->eff_price = (last_trade->eff_price * last_trade->size + k.eff_price*k.size)/(last_trade->size+k.size);
			last_trade->size += k.size;
			last_trade->eff_size += k.eff_size;
			last_trade->time = k.time;
		} else {
			res.new_trades.push_back(k);
			last_trade = &res.new_trades.back();
		}
	}

	res.brokerAssetBalance= tstatus.position;
	res.brokerCurrencyBalance = tstatus.balance;
	res.currencyUnadjustedBalance = *res.brokerCurrencyBalance + wcfg.externalBalance.lock_shared()->get(cfg.broker, minfo.wallet_id, minfo.currency_symbol);
	res.assetUnadjustedBalance = *res.brokerAssetBalance +  wcfg.externalBalance.lock_shared()->get(cfg.broker, minfo.wallet_id, minfo.asset_symbol);
	auto wdb = wcfg.walletDB.lock_shared();
	if (minfo.leverage == 0) {
		res.assetBalance = wdb->adjAssets(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),res.assetUnadjustedBalance);
		res.assetAvailBalance = wdb->adjAssets(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),*res.brokerAssetBalance);
	} else {
		res.assetBalance = res.assetUnadjustedBalance;
		res.assetAvailBalance = res.assetBalance;
	}
	res.currencyBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),	res.currencyUnadjustedBalance);
	res.currencyAvailBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),*res.brokerCurrencyBalance);


	auto ticker = tstatus.ticker;
	res.ticker = ticker;
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);

	if (ticker.bid < 0 || ticker.ask < 0 || ticker.bid > ticker.ask)
		throw std::runtime_error("Broker error: Ticker invalid values");

	res.chartItem.time = ticker.time;
	res.chartItem.bid = ticker.bid;
	res.chartItem.ask = ticker.ask;
	res.chartItem.last = ticker.last;

	auto step = calcSpread();
	res.curStep = step.spread;
	if (cfg.dynmult_sliding) {
		res.spreadCenter = step.center;
	} else {
		res.spreadCenter = 0;
	}

	res.tradingInstance = tstatus.instance;
	res.openOrders = std::move(tstatus.openOrders);

	return res;
}

bool MTrader::calculateOrderFeeLessAdjust(Order &order, double position, double currency, int dir, bool alerts, double min_size) const {

	//order is reversed to requested direction
	if (order.size * dir < 0) {
		//we can't trade this, so assume, that order size is zero
		order.size = 0;
		order.ar = AlertReason::strategy_outofsync;
	}

	if (order.size == 0) {
		//for forced, stoploss, disabled, we accept current order
		//otherwise it depends on "alerts enabled"
		if (order.alert != IStrategy::Alert::enabled) return true;
		else return alerts;
	}

	//check leverage
	double d;
	auto chkres = checkLeverage(order, position, currency, d);
	if (chkres != AlertReason::unknown)  {
		//adjust order when leverage reached
		order.size = d;
		//if result is zero
		if (d == 0) {
			//force alert
			order.alert = IStrategy::Alert::forced;
			order.ar = chkres;
			return true;
		}
	}

	//if size of order is above max_size, adjust to max_size
	if (cfg.max_size && std::fabs(order.size) > cfg.max_size) {
		order.size = cfg.max_size*dir;
	}
	//if order size is below min_size, adjust to zero
	if (std::fabs(order.size) < min_size) order.size = 0;
	//if order volume is below min volume (after add fees), adjust order to zero
	if (minfo.min_volume) {
		double op = order.price;
		double os = order.size;
		minfo.addFees(os,op);
		double vol = std::fabs(op * os);
		if (vol < minfo.min_volume) order.size = 0;
	}

	if (order.size == 0) {
		order.ar = AlertReason::below_minsize;
		return false;
	} else {
		//in this case, we continue to search better price (don't accept the order)
		return true;
	}

}

MTrader::Order MTrader::calculateOrderFeeLess(
		Strategy state,
		double prevPrice,
		double step,
		double dynmult,
		double curPrice,
		double position,
		double currency,
		bool alerts) const {

	double m = 1;

	int cnt = 0;
	double prevSz = 0;
	double sz = 0;
	Order order;

	double min_size = std::max(cfg.min_size, minfo.min_size);
	double dir = sgn(-step);

	double newPrice = prevPrice *exp(step*dynmult*m);
	if ((newPrice - curPrice) * dir > 0 || !std::isfinite(newPrice) || newPrice <= 0) {
		newPrice = curPrice;
		prevPrice = newPrice /exp(step*dynmult*m);
	}



	order= Order(
			state.getNewOrder(minfo,curPrice, newPrice,dir, position, currency, false),
			AlertReason::strategy_enforced
			);

	//Strategy can disable to place order using size=0 and disable alert
	if (order.size == 0 && order.alert == IStrategy::Alert::disabled) return order;

	bool skipcycle = false;

	if (order.price <= 0) order.price = newPrice;
	if ((order.price - curPrice) * dir < 0) {
		if (calculateOrderFeeLessAdjust(order, position, currency, dir, alerts, min_size)) skipcycle = true;;
	}
	double origOrderPrice = newPrice;


	if (!skipcycle) {


		do {
			prevSz = sz;

			newPrice = prevPrice * exp(step*dynmult*m);

			if ((newPrice - curPrice) * dir > 0) {
				newPrice = curPrice;
			}


			order= Order(
					state.getNewOrder(minfo,curPrice, newPrice,dir, position, currency, true),
					AlertReason::strategy_enforced
					);



			if (order.price <= 0) order.price = newPrice;
			if ((order.price - curPrice) * dir > 0) {
				order.price = curPrice;
			}

			sz = order.size;

			if (calculateOrderFeeLessAdjust(order, position, currency, dir, alerts, min_size)) break;;

			cnt++;
			m = m*1.1;

		} while (cnt < 1000 && order.size == 0 && ((sz - prevSz)*dir>0  || cnt < 10));
	}
	auto lmsz = limitOrderMinMaxBalance(position, order.size, order.price);
	if (lmsz.first != AlertReason::unknown) {
		order.size = lmsz.second;
		if (!order.size) {
			order.alert = IStrategy::Alert::forced;
		}
		order.ar = lmsz.first;
	}
	if (order.size == 0 && order.alert != IStrategy::Alert::disabled) {
		order.price = origOrderPrice;
		order.alert = IStrategy::Alert::forced;
	}

	return order;

}

MTrader::Order MTrader::calculateOrder(
		Strategy state,
		double lastTradePrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
		double currency,
		bool alerts) const {

		double fakeSize = -step;
		//remove fees from curPrice to effectively put order inside of bid/ask spread
		minfo.removeFees(fakeSize, curPrice);
		//calculate order
		Order order(calculateOrderFeeLess(state,lastTradePrice, step,dynmult,curPrice,balance,currency,alerts));
		//apply fees
		if (order.alert != IStrategy::Alert::stoploss && order.size) minfo.addFees(order.size, order.price);

		return order;

}

void MTrader::update_minfo() {
	minfo = stock->getMarketInfo(cfg.pairsymb);
	minfo.min_size = std::max(minfo.min_size, cfg.min_size);
}

void MTrader::initialize() {
	std::string brokerImg;
	IBrokerControl *bicon = dynamic_cast<IBrokerControl*>(stock.get());
	if (bicon)
		json::base64->encodeBinaryValue(json::map_str2bin(bicon->getBrokerInfo().favicon),[&](std::string_view c){
		brokerImg.append(c);
	});
	else
		brokerImg =
				"iVBORw0KGgoAAAANSUhEUgAAADAAAAAwBAMAAAClLOS0AAAAG1BMVEVxAAAAAQApKylNT0x8fnuX"
				"mZaztbLNz8z4+vcarxknAAAAAXRSTlMAQObYZgAAASFJREFUOMulkzFPxDAMhRMhmGNE72YGdhiQ"
				"boTtVkApN8KAriO3XPoDaPDPxk7SXpo6QogndfGnl/o5jlInaaNEwf0LSHX9ijhInhWS3gXDjoFg"
				"0Yi+Q1yCBvHuUjprjR5ghwcBDEZvRVBxrGr/SF3dLkHHObwQfc3gIM2KLF6cIiBemwqQx87AFKUo"
				"AkkJbLDQkAx9Cb7NL6BDl1ddBh4h01UGnvIm/wtSKso6B/SFVFu6kRFoYIBhpTgSgzCzG9svgH02"
				"6oxD8VFfp6NID+oi7jKAGX/ecOVTnQfHvF3Sm9J71y+AO5IfIAYM12ViwBQqgml9GOQjCQ/HC7Oq"
				"gvyoGZj2YwZqN/hhbWujWttm4I/rExvNNT6fxhWaRgeFuPYDghTP70Os5zoAAAAASUVORK5CYII=";

	try {
		update_minfo();
		if (!cfg.dont_allocate || cfg.enabled) {
			auto clk = wcfg.conflicts.lock();
			auto r = clk->get(cfg.broker, minfo.wallet_id, cfg.pairsymb);
			if (r != 0 && r != magic) {
			      throw std::runtime_error("Conflict: Can't run multiple traders on a single pair \r\n\r\n(To have a disabled trader on the same pair you have to enable 'No budget allocation' on the disabled trader)");
			}
			clk->put(cfg.broker, minfo.wallet_id, cfg.pairsymb, magic);
		}

		if (!cfg.hidden) {
			this->statsvc->setInfo(
				IStatSvc::Info { cfg.title, minfo.asset_symbol,
						minfo.currency_symbol,
								minfo.invert_price ?
										minfo.inverted_symbol :
										minfo.currency_symbol, brokerImg,
										cfg.broker,
										minfo.wallet_id,
						cfg.report_order,
						minfo.invert_price, minfo.leverage != 0, minfo.simulator });
		}

	} catch (std::exception &e) {
		if (!cfg.hidden) {
			this->statsvc->setInfo(
					IStatSvc::Info {cfg.title, "???",
									"???",
									"???",
									brokerImg,
									cfg.broker,
									"???",
									cfg.report_order,
									false,
									false,
									true});
			this->statsvc->reportError(IStatSvc::ErrorObj(e.what()));
			throw;
		}
	}
}

void MTrader::loadState() {
	if (storage == nullptr) return;
	auto st = storage->load();
	need_load = false;


	if (st.defined()) {


		auto state = st["state"];
		if (state.defined()) {
			dynmult.setMult(state["buy_dynmult"].getNumber(),state["sell_dynmult"].getNumber());
			position_valid = state["internal_balance"].hasValue();
			if (position_valid) {
				position = state["internal_balance"].getNumber();
			} else {
				position = 0;
			}
			if (state["currency_balance"].hasValue()) {currency = state["currency_balance"].getNumber(); currency_valid = true;}
			json::Value accval = state["account_value"];
			recalc = state["recalc"].getBool();
			std::size_t nuid = state["uid"].getUInt();
			if (nuid) uid = nuid;
			tradingInstance = state["tradingInstance"];
			lastPriceOffset = state["lastPriceOffset"].getNumber();
			lastTradePrice = state["lastTradePrice"].getNumber();
			frozen_spread_side = state["frozen_side"].getInt();
			frozen_spread = state["frozen_spread"].getNumber();
			expPos.fromJSON(state["expPos"]);

			bool cfg_sliding = state["cfg_sliding_spread"].getBool();
			if (cfg_sliding != cfg.dynmult_sliding)
				lastPriceOffset = 0;
			achieve_mode = state["achieve_mode"].getBool();
			need_initial_reset = state["need_initial_reset"].getBool();
			adj_wait = state["adj_wait"].getUInt();
			adj_wait_price = state["adj_wait_price"].getNumber();
			accumulated = state["accumulated"].getNumber();
		}
		auto chartSect = st["chart"];
		if (chartSect.defined()) {
			chart.clear();
			for (json::Value v: chartSect) {
				double ask = v["ask"].getNumber();
				double bid = v["bid"].getNumber();
				double last = v["last"].getNumber();
				if (minfo.invert_price) {
					ask = 1.0/ask;
					bid = 1.0/bid;
					last = 1.0/last;
				}
				std::uint64_t tm = v["time"].getUIntLong();

				chart.push_back({tm,ask,bid,last});
			}
		}
		{
			auto trSect = st["trades"];
			if (trSect.defined()) {
				trades.clear();
				for (json::Value v: trSect) {
					TWBItem itm = TWBItem::fromJSON(v);
					trades.push_back(itm);
				}
			}
		}
		strategy.importState(st["strategy"], minfo);


	}
	tempPr.broker = cfg.broker;
	tempPr.magic = magic;
	tempPr.uid = uid;
	tempPr.currency = minfo.currency_symbol;
	tempPr.asset = minfo.asset_symbol;
	tempPr.simulator = minfo.simulator;
	tempPr.invert_price = minfo.invert_price;
	if (strategy.isValid() && !trades.empty()) {
		wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(trades.back().eff_price, minfo.leverage>0));
	}
	if (minfo.leverage == 0) {
		if (position_valid) {
			double accum = getAccumulated();
			wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position+accum);
			wcfg.accumDB.lock()->alloc(getWalletAssetKey(), accum);

		}
	}
	updateEnterPrice();


}

void MTrader::saveState() {
	if (storage == nullptr || need_load) return;
	json::Object obj;

	{
		auto st = obj.object("state");
		st.set("buy_dynmult", dynmult.getBuyMult());
		st.set("sell_dynmult", dynmult.getSellMult());
		if (position_valid)
			st.set("internal_balance", position);
		if (currency_valid)
			st.set("currency_balance", currency);
		st.set("recalc",recalc);
		st.set("uid",uid);
		st.set("tradingInstance",tradingInstance);
		st.set("lastPriceOffset",lastPriceOffset);
		st.set("lastTradePrice", lastTradePrice);
		st.set("cfg_sliding_spread",cfg.dynmult_sliding);
		st.set("private_chart", minfo.private_chart||minfo.simulator);
		st.set("accumulated",accumulated);
		st.set("frozen_side", frozen_spread_side);
		st.set("frozen_spread", frozen_spread);
		st.set("exp_pos", expPos.toJSON());
		if (achieve_mode) st.set("achieve_mode", achieve_mode);
		if (need_initial_reset) st.set("need_initial_reset", need_initial_reset);
		st.set("adj_wait",adj_wait);
		if (adj_wait) st.set("adj_wait_price", adj_wait_price);
	}
	{
		auto ch = obj.array("chart");
		for (auto &&itm: chart) {
			ch.push_back(json::Object({{"time", itm.time},
				{"ask",minfo.invert_price?1.0/itm.ask:itm.ask},
				{"bid",minfo.invert_price?1.0/itm.bid:itm.bid},
				{"last",minfo.invert_price?1.0/itm.last:itm.last}}));
		}
	}
	{
		auto tr = obj.array("trades");
		for (auto &&itm:trades) {
			tr.push_back(itm.toJSON());
		}
	}
	obj.set("strategy",strategy.exportState());
	storage->store(obj);
}


bool MTrader::eraseTrade(std::string_view id, bool trunc) {
	init();
	auto iter = std::find_if(trades.begin(), trades.end(), [&](const IStockApi::Trade &tr) {
		json::String s = tr.id.toString();
		return s.str() == id;
	});
	if (iter == trades.end()) return false;
	if (trunc) {
		trades.erase(iter, trades.end());
	} else {
		trades.erase(iter);
	}
	saveState();
	return true;
}


bool MTrader::processTrades(Status &st) {


	//Remove duplicate trades
	//which can happen by failed synchronization
	//while the new trade is already in current trades
	auto iter = std::remove_if(st.new_trades.begin(), st.new_trades.end(),
			[&](const IStockApi::Trade &t) {
				return std::find_if(trades.begin(), trades.end(),[&](const IStockApi::Trade &q) {
					return t.id == q.id;
				}) != trades.end();
	});

	st.new_trades.erase(iter, st.new_trades.end());


	double assetBal = position;
	double curBal = st.currencyBalance - (minfo.leverage?0:std::accumulate(
			st.new_trades.begin(), st.new_trades.end(),0,[](double a, const IStockApi::Trade &tr){
				return a - tr.price*tr.size;
			}));


	double last_np = 0;
	double last_ap = 0;
	double last_price = 0;
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;
		last_price = trades.back().eff_price;
	}

	bool res = false;


	for (auto &&t : st.new_trades) {
		if (t.eff_price <= 0 || t.price <= 0) throw std::runtime_error("Broker error - trade negative price");

		res = true;

		auto new_acb = acb_state(t.eff_price, t.eff_size);

		tempPr.tradeId = t.id.toString().str();
		tempPr.size = t.eff_size;
		tempPr.price = t.eff_price;
		tempPr.change = assetBal * (t.eff_price - last_price);
		tempPr.time = t.time;
		tempPr.acb_pnl = new_acb.getRPnL() - acb_state.getRPnL();
		tempPr.position = assetBal+t.eff_size;
		if (last_price) statsvc->reportPerformance(tempPr);
		last_price = t.eff_price;
		if (minfo.leverage == 0) curBal -= t.price * t.size;
		spent_currency += t.price*t.size;

		acb_state = new_acb;

		assetBal += t.eff_size;

		if (!achieve_mode && (cfg.enabled || first_cycle)) {
		    auto ss = strategy;
            expPos.partial = expPos.partial(t.eff_price, t.eff_size);
			auto norm = ss.onTrade(minfo, expPos.partial.getPos(), expPos.partial.getOpen(), assetBal, curBal);
            double np_diff = norm.normProfit  - expPos.norm_profit + expPos.partial.getRPnL();
            double na_diff = norm.normAccum - expPos.norm_accum ;
            expPos.norm_profit += np_diff;
            expPos.norm_accum += na_diff;
            last_np += np_diff;
            last_ap += na_diff;
            assetBal -= na_diff;
            t.eff_size-= na_diff;
            accumulated +=na_diff;
            trades.push_back(TWBItem(t, last_np, last_ap, 0, true));
		} else {
			trades.push_back(TWBItem(t, last_np, last_ap, 0, true));
		}
	}
//	wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(last_price, minfo.leverage>0));

	if (position_valid) position = assetBal;
	else position = st.assetBalance;
	position_valid = true;

	return res;
}

void MTrader::update_dynmult(bool buy_trade,bool sell_trade) {
	dynmult.update(buy_trade, sell_trade);
}

void MTrader::clearStats() {
	init();
	trades.clear();
	position = 0;
	position_valid = false;
	adj_wait = 0;
	adj_wait_price = 0;
	accumulated = 0;
	saveState();
	updateEnterPrice();
}

void MTrader::stop() {
	cfg.enabled = false;
}

void MTrader::reset(const ResetOptions &ropt) {
	init();
	dynmult.setMult(1, 1);
	stock->reset(std::chrono::system_clock::now());

	tradingInstance = nullptr;

	lastPriceOffset = 0;
	double lastPrice = 0;
	for (auto &&x : trades) {
		if (!std::isfinite(x.norm_accum)) x.norm_accum = 0;
		if (!std::isfinite(x.norm_profit)) x.norm_profit = 0;
		if (x.price < 1e-20 || !std::isfinite(x.price)) {
			x.price = lastPrice;
		} else {
			lastPrice = x.price;
		}
	}
	auto status = getMarketStatus();
	double assets;
	if (position_valid && !trades.empty()) {
		assets = position;
	} else {
		position = assets = wcfg.walletDB.lock_shared()->adjBalance(getWalletAssetKey(), status.assetUnadjustedBalance)-accumulated;
		accumulated = 0;
	}
	if (!trades.empty()) status.curPrice = trades.back().price;
	position_valid = true;
	double currency = status.currencyUnadjustedBalance;
	double position = ropt.achieve?(minfo.invert_price?-1.0:1.0)*(ropt.assets):assets;
	currency = wcfg.walletDB.lock_shared()->adjBalance(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid), currency);
	double fin_cur = currency * ropt.cur_pct;
	double diff = position - assets;
	double vol = diff * status.curPrice;
	double remain = minfo.leverage?fin_cur:std::max(fin_cur - vol,0.0);
	logInfo("RESET strategy: price=$1, cur_pos=$2, new_pos=$3, diff=$4, volume=$5, remain=$6", status.curPrice, assets, position, diff, vol, remain);
	strategy.reset();
	try {
		strategy.onIdle(minfo, status.ticker, position, remain);
		achieve_mode = ropt.achieve;
		need_initial_reset = false;
		wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(status.curPrice, minfo.leverage>0));
		if (minfo.leverage == 0) {
				wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position+accumulated);
				wcfg.accumDB.lock()->alloc(getWalletAssetKey(), accumulated);
		}

	} catch (...) {
		need_initial_reset = true;
		saveState();
		throw;
	}

	saveState();
}
#if 0
void MTrader::reset(std::optional<double> achieve_pos) {
	init();
	dynmult.setMult(1,1);
/*	if (cfg.internal_balance) {
		if (!trades.empty())
			asset_balance = std::accumulate(trades.begin(), trades.end(), 0.0, [](double v, const TWBItem &itm) {return v+itm.eff_size;});
	} else {
		asset_balance.reset();
		currency_balance.reset();
	}
	currency_balance.reset();*/
	achieve_mode = false;

	if (!trades.empty()) {
		stock->reset();
		lastTradeId = nullptr;
	}
	double lastPrice = 0;
	lastPriceOffset = 0;
	for (auto &&x : trades) {
		if (!std::isfinite(x.norm_accum)) x.norm_accum = 0;
		if (!std::isfinite(x.norm_profit)) x.norm_profit = 0;
		if (x.price < 1e-8 || !std::isfinite(x.price)) {
			x.price = lastPrice;
		} else {
			lastPrice = x.price;
		}
	}

	strategy.reset();
	if (achieve_pos.has_value()) {
		double position = (minfo.invert_price?-1.0:1.0)*(*achieve_pos);
		auto tk = stock->getTicker(cfg.pairsymb);
		auto alloc = walletDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid));
		auto cur = stock->getBalance(minfo.currency_symbol, cfg.pairsymb)+cfg.external_balance-alloc.otherTraders;
		auto ass = stock->getBalance(minfo.asset_symbol, cfg.pairsymb);
		double diff = position - ass;
		double vol = diff * tk.last;
		double remain = minfo.leverage?cur:std::max(cur - vol,0.0);
		logInfo("RESET strategy: price=$1, cur_pos=$2, new_pos=$3, diff=$4, volume=$5, remain=$6", tk.last, ass, position, diff, vol, remain);
		strategy.onIdle(minfo, tk, position, remain);
		achieve_mode = true;
	}


	need_initial_reset = false;
	saveState();
}
#endif
MTrader::Chart MTrader::getChart() const {
	return chart;
}


void MTrader::addAcceptLossAlert() {
	Status st = getMarketStatus();
	alertTrigger(st, st.ticker.last, 0, AlertReason::initial_reset);
}

void MTrader::acceptLoss(const Status &st, double dir) {

	if (cfg.accept_loss && cfg.enabled && !trades.empty()) {
		std::size_t ttm = trades.back().time;

		if (dynmult.getBuyMult() <= 1.0 && dynmult.getSellMult() <= 1.0) {
			std::size_t e = st.chartItem.time>ttm?(st.chartItem.time-ttm)/(3600000):0;
			double lastTradePrice = trades.back().eff_price;
			auto order = calculateOrder(strategy, lastTradePrice, -st.curStep * 2 * dir, 1, lastTradePrice, st.assetBalance, st.currencyBalance, true);
			double limitPrice = order.price;
			if (e > cfg.accept_loss && (st.curPrice - limitPrice) * dir < 0) {
				ondra_shared::logWarning("Accept loss in effect: price=$1, balance=$2", st.curPrice, st.assetBalance);
				alertTrigger(st, limitPrice, dir, AlertReason::accept_loss);
				strategy.reset();
			}
		}
	}
}

void MTrader::dropState() {
	storage->erase();
}


template<typename Iter>
MTrader::SpreadCalcResult MTrader::stCalcSpread(Iter beg, Iter end, unsigned int input_sma, unsigned int input_stdev) {
	input_sma = std::max<unsigned int>(input_sma,30);
	input_stdev = std::max<unsigned int>(input_stdev,30);
	std::queue<double> sma;
	std::vector<double> mapped;
	double avg = 0;
	std::accumulate(beg, end, 0.0, [&](auto &&a, auto &&c) {
		double h = 0.0;
		if ( sma.size() >= input_sma) {
			h = sma.front();
			sma.pop();
		}
		double d = a + c - h;
		sma.push(c);
		avg = d/sma.size();
		mapped.push_back(c - avg);
		return d;
	});

	std::size_t i = mapped.size() >= input_stdev?mapped.size()-input_stdev:0;
	auto iter = mapped.begin()+i;
	auto mend = mapped.end();
	auto stdev = std::sqrt(std::accumulate(iter, mend, 0.0, [&](auto &&v, auto &&c) {
		return v + c*c;
	})/std::distance(iter, mend));
	return SpreadCalcResult{
		std::log((stdev+avg)/avg),
		avg
	};
}

std::optional<double> MTrader::getPosition() const {
	return position;
}

std::optional<double> MTrader::getCurrency() const {
	return currency;
}



MTrader::SpreadCalcResult MTrader::calcSpread() const {

	ISpreadFunction::Result r;
	auto state = spread_fn->start();
	for (const auto &x: chart) {
		r = spread_fn->point(state, x.last);
	}
	if (r.valid) {
		return {
			r.spread,r.center
		};
	} else {
		return SpreadCalcResult{0,0};
	}



}


bool MTrader::checkMinMaxBalance(double balance, double orderSize, double price) const {
	auto x = limitOrderMinMaxBalance(balance, orderSize, price);
	return x.first != AlertReason::unknown;
}

std::pair<AlertReason, double> MTrader::limitOrderMinMaxBalance(double balance, double orderSize, double price) const {
	const auto &min_balance = minfo.invert_price?cfg.max_balance:cfg.min_balance;
	const auto &max_balance = minfo.invert_price?cfg.min_balance:cfg.max_balance;
	double factor = minfo.invert_price?-1:1;

	if (orderSize < 0) {
		if (min_balance.has_value()) {
			double m = *min_balance * factor;
			if (balance < m) return {AlertReason::position_limit,0};
			if (balance+orderSize < m) return {AlertReason::position_limit,m - balance};
		}

	} else {
		if (max_balance.has_value()) {
			double m = *max_balance * factor;
			if (balance > m) return {AlertReason::position_limit,0};
			if (balance+orderSize > m) return {AlertReason::position_limit,m - balance};
		}

	}
	if (cfg.max_costs.has_value() && orderSize * balance >= 0) {
		double cost = orderSize * price;
		if (cost + spent_currency > cfg.max_costs) {
			return {AlertReason::max_cost,0};
		}
	}
	return {AlertReason::unknown,orderSize};
}

void MTrader::setInternalBalancies(double assets, double cur) {
	position = assets;
	position_valid =true;
	currency= cur;
	currency_valid = true;
}



bool MTrader::checkAchieveModeDone(const Status &st) {
	double eq = strategy.getEquilibrium(st.assetBalance);
	if (!std::isfinite(eq)) return false;
	double low = eq * std::exp(-st.curStep);
	double hi = eq * std::exp(st.curStep);
	return st.curPrice >= low && st.curPrice <=hi;

}

WalletDB::Key MTrader::getWalletBalanceKey() const {
	return WalletDB::Key{
		cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid
	};
}

WalletDB::Key MTrader::getWalletAssetKey() const {
	return WalletDB::Key{
		cfg.broker, minfo.wallet_id, minfo.asset_symbol, uid
	};
}

bool MTrader::checkEquilibriumClose(const Status &st, double lastTradePrice) {
	double eq = strategy.getEquilibrium(st.assetBalance);
	if (!std::isfinite(eq)) return false;
	double low = eq * std::exp(-st.curStep);
	double hi = eq * std::exp(st.curStep);
	return lastTradePrice >= low && lastTradePrice <=hi;

}

AlertReason MTrader::checkLeverage(const Order &order,double position, double currency, double &maxSize) const {
	double whole_pos = order.size + position;
	if (cfg.trade_within_budget && order.size * position > 0) {
		double alloc = strategy.calcCurrencyAllocation(order.price, minfo.leverage);
		if (alloc < 0) {
			maxSize = 0;
			return AlertReason::out_of_budget;
		}
	}
	if (minfo.leverage && cfg.max_leverage) {
		if (order.size * position < 0)
			return AlertReason::unknown; //position reduce

		double bal = currency;

		if (!trades.empty()) {
			double chng = order.price - trades.back().eff_price;
			bal += chng * position;
		}
		double max_bal = bal * cfg.max_leverage ;
		double cash_flow = std::abs((order.size + position)  * order.price);
		if (cash_flow > max_bal) {
			maxSize = (max_bal / order.price - std::abs(position));
			if (maxSize < 0) maxSize = 0;
			maxSize *= sgn(order.size);
			return AlertReason::max_leverage;
		}
	} else if (minfo.leverage == 0 && cfg.accept_loss == 0) {
		if (whole_pos < 0) {
			maxSize = -std::max(position,0.0);
			return AlertReason::no_funds;
		}
		double vol = order.size * order.price;
		double min_cur = vol*0.01;
		if (currency - vol < min_cur) {
			vol = currency - min_cur;
			maxSize = std::max(vol/order.price,0.0);
			return AlertReason::no_funds;
		}
	}
	return AlertReason::unknown;

}

bool MTrader::checkReduceOnLeverage(const Status &st, double &maxPosition) {
	if (!cfg.reduce_on_leverage) return false;
	if (minfo.leverage <= 0) return false;
	if (cfg.max_leverage <=0) return false;
	double mp = currency/st.curPrice * cfg.max_leverage;
	if (mp < std::abs(st.assetBalance)) {
		maxPosition = sgn(st.assetBalance) * mp;
		return true;
	} else {
		return false;
	}
}

MTrader::BalanceChangeEvent MTrader::detectLeakedTrade(const Status &st) const {
	if (position_valid && cfg.enabled) {
		double minsize = std::max({minfo.asset_step, minfo.min_size, minfo.min_volume/st.curPrice});
		double accum = getAccumulated();
		double diff = st.assetBalance - (position+accum);
		if (std::abs(diff)<minsize) return BalanceChangeEvent::no_change; //this is ok - difference is on round error level
		if (minfo.leverage == 0 && diff<0) {//we can detect leaked trade on exchange better then on futures
			double cchg = std::abs(diff * st.curPrice);
			double cdiff = std::abs(st.currencyBalance-currency);
			if (cdiff < cchg*0.25) {
				auto x = wcfg.accumDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.asset_symbol, uid));
				double accumTot = x.otherTraders+x.thisTrader;
				if (accumTot+diff > -accumTot) {
					return BalanceChangeEvent::withdraw;
				}
			}
		}
		return BalanceChangeEvent::leak_trade;
	} else {
		return BalanceChangeEvent::disabled;
	}
}

void MTrader::doWithdraw(const Status &st) {
	double accum = getAccumulated();
	double diff = st.assetBalance - (position+accum);
	double withdraw_size;
	auto wkey = getWalletAssetKey();
	auto x = wcfg.accumDB.lock_shared()->query(wkey);
	if (x.otherTraders == 0) {
		withdraw_size = diff;
	} else {
		withdraw_size = std::max(diff, - accum);
	}
	accum+=withdraw_size;
	logInfo("Withdraw from trader: amount=$1, remain=$2, accumulated=$3, accumulated_total=$4", withdraw_size, diff-withdraw_size,accum,x.thisTrader+x.otherTraders+withdraw_size);
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	trades.push_back(IStatSvc::TradeRecord(IStockApi::Trade{
		"WITHDRAW:"+std::to_string(time),
		static_cast<std::uint64_t>(time),
		0, lastTradePrice, 0, lastTradePrice
	},0,accum,0,true));
	wcfg.accumDB.lock()->alloc(std::move(wkey), accum);
}

double MTrader::getAccumulated() const {
	return accumulated;
}

void MTrader::recalcNorm() {
	init();
	if (trades.empty()) return;
	auto st = getMarketStatus();
	double cur = st.currencyBalance;
	double pos = st.assetBalance;
	double sumsz = std::accumulate(trades.begin(), trades.end(), 0.0, [&](double x, const IStatSvc::TradeRecord &rec){
		return x+rec.eff_size;
	});
	pos-=sumsz;
	if (minfo.leverage) {
		double price = trades[0].eff_price;
		double mypos = pos+ trades[0].eff_size;
		double pnl = 0;
		for (std::size_t i=1,cnt = trades.size(); i < cnt; i++) {
			pnl += (trades[i].eff_price - price)*mypos;
			mypos += trades[i].eff_size;
			price = trades[i].eff_price;
		}
		cur -= pnl;
	} else {
		double sumpr = std::accumulate(trades.begin(), trades.end(), 0.0, [&](double x, const IStatSvc::TradeRecord &rec){
			return x-rec.eff_size*rec.eff_price;
		});
		cur -= sumpr;
	}
	Strategy z = strategy;
	z.reset();
	auto getTicker = [](double p, std::uint64_t tm) {
		return IStockApi::Ticker{p,p,p,tm};
	};
	double prevTrade = trades[0].eff_price;
	double normp = 0, norma = 0;
	for (std::size_t i=0, cnt = trades.size(); i< cnt; i++) {
		z.onIdle(minfo, getTicker(trades[i].eff_price,trades[i].time), pos, cur);
		double newpos = trades[i].eff_size+ pos;
		double newcur = minfo.leverage?(trades[i].eff_price-prevTrade)*pos:cur-trades[i].eff_size*trades[i].eff_price;
		auto res = z.onTrade(minfo, trades[i].eff_price, trades[i].eff_size, newpos, newcur);
		if (!std::isfinite(res.normProfit)) res.normProfit = 0;
		if (!std::isfinite(res.normAccum)) res.normAccum = 0;
		if (trades[i].manual_trade){
			res.normAccum = 0;
			res.normProfit = 0;
		}
		normp += res.normProfit;
		norma += res.normAccum;
		trades[i].norm_profit = normp;
		trades[i].norm_accum = norma;
		trades[i].neutral_price = res.neutralPrice;
		cur = newcur;
		pos = newpos-=res.normAccum;
	}
	saveState();
}

void MTrader::fixNorm() {
	init();
	if (trades.empty()) return;
	double normp = std::accumulate(trades.begin(), trades.end(),0.0,[z=0.0](double x,const IStatSvc::TradeRecord &rec) mutable {
		double df = std::abs(rec.norm_profit-z);
		z = rec.norm_profit;
		return x+df;
	});
	double norma = std::accumulate(trades.begin(), trades.end(),0.0,[z=0.0](double x,const IStatSvc::TradeRecord &rec) mutable {
		double df = std::abs(rec.norm_accum-z);
		z = rec.norm_accum;
		return x+df;
	});
	double avgp = normp/trades.size();
	double avga = norma/trades.size();
	double maxp = 2*std::abs(avgp);
	double maxa = 2*std::abs(avga);
	double cura = 0;
	double curp = 0;
	double lasta = 0;
	double lastp = 0;
	for (std::size_t i = 0,cnt = trades.size(); i < cnt; i++) {
		double a = trades[i].norm_accum;
		double p = trades[i].norm_profit;
		double dfa =  a - lasta;
		double dfp =  p - lastp;
		if (!std::isfinite(dfa) || std::abs(dfa) > maxa) {
			dfa = 0;
		}
		if (!std::isfinite(dfp) || std::abs(dfp) > maxp) {
			dfp = 0;
		}
		cura += dfa;
		curp += dfp;
		lasta = a;
		lastp = p;
		trades[i].norm_accum = cura;
		trades[i].norm_profit = curp;
	}
	saveState();
}

double MTrader::getEnterPrice() const {
	return acb_state.getOpen();
}
double MTrader::getEnterPricePos() const {
	return acb_state.getPos();
}

double MTrader::getCosts() const {
	return spent_currency;
}
double MTrader::getRPnL() const {
	return acb_state.getRPnL();
}

void MTrader::updateEnterPrice() {
	double initState = (position_valid?position:0) - std::accumulate(trades.begin(), trades.end(), 0.0, [&](double a, const auto &tr) {
		return a + tr.eff_size;
	});
	double openPrice = cfg.init_open;
	if (openPrice == 0) {
		if (!trades.empty()) openPrice = trades[0].eff_price;
	} else {
		if (minfo.invert_price) openPrice = 1.0/openPrice;
	}

	ACB acb(openPrice, initState);
	spent_currency = 0;
	for (const auto &tr: trades) {
		acb = acb(tr.eff_price, tr.eff_size);
		spent_currency += tr.size * tr.price;
	}
	acb_state = acb;
}

void MTrader::OrderMgr::load(IStockApi::Orders &&opened) {
    this->opened = std::move(opened);
    to_place.clear();
    tmp.clear();
    place_errors.clear();
    masked.clear();
    masked.resize(opened.size(), false);
}

void MTrader::OrderMgr::placeOrder(const Order &ord, int localId) {
    if (ord.size || ord.alert == IStrategy::Alert::forced) {
            to_place.push_back({ord.price,ord.size,ord.ar,localId,0});
    }
}

void MTrader::OrderMgr::commit(IStockApi &target, const IStockApi::MarketInfo &minfo, const std::string_view &pair, json::Value &instance) {
    tmp.clear();

    for (NewOrder &ord: to_place)  {
        if (ord.size) {
            auto itr = std::find_if(opened.begin(), opened.end(), [&](const IStockApi::Order &x) -> bool {
                auto idx = &x - &opened[0];
                if (!masked[idx] && std::abs(x.price - ord.price) < minfo.currency_step
                       && std::abs(x.size - ord.size) < minfo.asset_step) {
                   return true;
               } else {
                   return false;
               }
            });
            if (itr != opened.end()) {
                int d = std::distance(opened.begin(), itr);
                masked[d] = true;
                ord.to_place_index = -1;
                ord.reuse_index = d;
            }
        } else {
            alerts.push_back(AlertInfo{ord.price, ord.ar});
            ord.to_place_index = -1;
        }
    }

    for (NewOrder &ord: to_place) if (ord.to_place_index == 0) {
        auto distance = std::numeric_limits<double>::max();
        int best_index = -1;
        for (std::size_t i = 0, cnt = opened.size(); i < cnt; i++) if (!masked[i]) {
            if (std::signbit(opened[i].size) == std::signbit(ord.size)) {
                auto d = std::abs(opened[i].price - ord.price);
                if (d < distance) best_index = i;
            }
        }
        if (best_index >= 0) {
            ord.to_place_index = tmp.size();
            tmp.push_back({
               ord.price,ord.size,opened[best_index].id,0
            });
            masked[best_index] = true;
        } else {
            ord.to_place_index = tmp.size();
            tmp.push_back({
               ord.price,ord.size,nullptr,0
            });
        }
    }
    for (std::size_t i = 0, cnt = opened.size(); i < cnt; i++) if (!masked[i]) {
        tmp.push_back({
            0,0,opened[i].id,0
        });
    }

    target.placeOrders(pair, tmp, instance);

    for (const auto &x: tmp) {
        if (!x.placed && !x.error.empty()) {
            place_errors.push_back(x.error);
        } else {
            place_errors.push_back({});
        }
    }

}

void MTrader::OrderMgr::commit_force_cancel(IStockApi &target, const std::string_view &pair, json::Value &instance) {
    tmp.clear();
    for (const auto &o: opened) {
        tmp.push_back({0,0,o.id,0});
    }
    for (auto &o: to_place) {
        o.to_place_index = tmp.size();
        tmp.push_back({o.price, o.size, nullptr,0});
    }

    target.placeOrders(pair, tmp, instance);

    for (const auto &x: tmp) {
        if (!x.placed && !x.error.empty()) {
            place_errors.push_back(x.error);
        } else {
            place_errors.push_back({});
        }
    }
}

std::optional<MTrader::OrderPlaceResult> MTrader::OrderMgr::findPlacedOrderById(int localId) const {
    auto iter = std::find_if(to_place.begin(), to_place.end(), [&](const NewOrder &ord) {
       return ord.custom_id == localId;
    });

    if (iter == to_place.end()) return {};
    IStockApi::Order ord{json::Value(), json::Value(),iter->size, iter->price};
    if (iter->reuse_index >= 0) return OrderPlaceResult{opened[iter->reuse_index]};
    if (iter->to_place_index>=0) {
        return OrderPlaceResult{
            ord,place_errors[iter->to_place_index]
        };
    }
    if (iter->size) return {};
    return OrderPlaceResult{
        ord
    };

}

