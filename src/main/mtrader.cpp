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


	min_size = data["min_size"].getValueOrDefault(0.0);
	max_size = data["max_size"].getValueOrDefault(0.0);
	json::Value min_balance = data["min_balance"];
	json::Value max_balance = data["max_balance"];
	json::Value max_costs = data["max_costs"];
	if (min_balance.type() == json::number) this->min_balance = min_balance.getNumber();
	if (max_balance.type() == json::number) this->max_balance = max_balance.getNumber();
	if (max_costs.type() == json::number) this->max_costs = max_costs.getNumber();


	spread = create_spread_generator(data);


	adj_timeout = std::max<unsigned int>(5,data["adj_timeout"].getUInt());
	max_leverage = data["max_leverage"].getValueOrDefault(0.0);

	report_order = data["report_order"].getValueOrDefault(0.0);
	secondary_order_distance = data["secondary_order"].getValueOrDefault(0.0)*0.01;
	grant_trade_minutes = static_cast<unsigned int>(data["grant_trade_hours"].getValueOrDefault(0.0)*60);


	if (data["dry_run"].getBool() == true) throw std::runtime_error("Paper trading option is no longer supported");
	paper_trading = data["paper_trading"].getValueOrDefault(false);
	dont_allocate = data["dont_allocate"].getValueOrDefault(false) ;
	enabled= data["enabled"].getValueOrDefault(true);
	hidden = data["hidden"].getValueOrDefault(false);
	emulate_leveraged=data["emulate_leveraged"].getValueOrDefault(0.0);
	trade_within_budget = data["trade_within_budget"].getBool();
	init_open = data["init_open"].getNumber();

	if (paper_trading) {
		paper_trading_src_state = data["pp_source"].getString();
	}


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
,acb_state(0,0)
,partial_eff_pos(0,0)
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

void MTrader::alertTrigger(const Status &st, double price, int dir, AlertReason reason) {
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
		trades.push_back(TWBItem(tr, last_np+=norm.normProfit, last_ap+=norm.normAccum, norm.neutralPrice, false,false, static_cast<char>(dir), static_cast<char>(reason)));
	} else {
		trades.push_back(TWBItem(tr, last_np, last_ap, 0, false, true, static_cast<char>(dir), static_cast<char>(reason)));
	}
	refresh_minfo = true;
}

void MTrader::dorovnani(Status &st, double assetBalance, double price) {
	double diff = st.assetBalance - assetBalance-getAccumulated();
	st.new_trades.trades.push_back(IStockApi::Trade{
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

		//Get opened orders
		auto orders = getOrders();
		//get current status
		auto status = getMarketStatus();

		if (status.brokerCurrencyBalance.has_value()) {
			wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.currency_symbol, *status.brokerCurrencyBalance);
		}
		if (minfo.leverage == 0 && position_valid) {
			double accum = getAccumulated();
			if (status.brokerAssetBalance.has_value()) wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.asset_symbol, *status.brokerAssetBalance);
			wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position+accum);
			wcfg.accumDB.lock()->alloc(getWalletAssetKey(), accum);
		}

		bool delayed_trade_detect = false;
		std::string buy_order_error;
		std::string sell_order_error;
		//process all new trades
		bool anytrades = processTrades(status);

		if (anytrades && (achieve_mode || !cfg.enabled)) {
		    flush_partial(status);
		}

		strategy_position = position-partial_position;

        double eq = strategy.getEquilibrium(strategy_position);


		BalanceChangeEvent bche = detectLeakedTrade(status);
		switch (bche) {
		default:
		case BalanceChangeEvent::disabled: break;
		case BalanceChangeEvent::leak_trade:
			if (adj_wait>cfg.adj_timeout) {
				dorovnani(status, position, adj_wait_price);
				anytrades = processTrades(status);
				logNote("Adjust added: result - $1", position);
				adj_wait = 0;
			} else {
				if (adj_wait == 0) adj_wait_price = status.curPrice;
				adj_wait++;
				logNote("Need adjust $1 => $2, stage: $3/$4 price: $5",  position, status.assetBalance-getAccumulated(), adj_wait ,cfg.adj_timeout, adj_wait_price);
				delayed_trade_detect = true;
			}
			break;
		case BalanceChangeEvent::withdraw:
			doWithdraw(status);
			currency = status.currencyBalance;
			currency_valid = true;
			adj_wait = 0;
			break;
		case BalanceChangeEvent::no_change:
			currency = status.currencyBalance;
			currency_valid = true;
			adj_wait = 0;
			break;
		}

		bool need_alerts = trades.empty()?false:trades.back().size == 0;

		double lastTradeSize = trades.empty()?0:trades.back().eff_size;
		if (lastTradePrice == 0 ) {
			lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium(status.assetBalance):status.curPrice;
			if (!std::isfinite(lastTradePrice)) lastTradePrice = status.curPrice;		}

		double centerPrice = need_alerts?lastTradePrice:strategy.getCenterPrice(lastTradePrice, strategy_position);


        bool grant_trade = cfg.grant_trade_minutes
                && !trades.empty()
                && status.ticker.time > trades.back().time
                && (status.ticker.time - trades.back().time)/60000 > cfg.grant_trade_minutes;


        if (achieve_mode) {
            achieve_mode = !checkAchieveModeDone(status);
            grant_trade |= achieve_mode;
        }

        if (trade_now_mode) {
            grant_trade = true;
        }
        if (anytrades) grant_trade = false;



        if (status.new_trades.trades.empty()) {
            //process alerts
            if (sell_alert.has_value() && status.ticker.last >= sell_alert->price) {
                alertTrigger(status, sell_alert->price,-11,sell_alert->reason);
                lastTradePrice=sell_alert->price;
                cfg.spread->point(spread_state, sell_alert->price, true);
                sell_alert.reset();
                trade_now_mode = false;
            }
            if (buy_alert.has_value() && status.ticker.last <= buy_alert->price) {
                alertTrigger(status, buy_alert->price,1,buy_alert->reason);
                lastTradePrice=buy_alert->price;
                cfg.spread->point(spread_state, buy_alert->price, true);
                buy_alert.reset();
                trade_now_mode = false;
            }
        }


        strategy.onIdle(minfo, status.ticker, strategy_position, status.currencyBalance);


        if (!cfg.enabled || need_initial_reset || delayed_trade_detect)  {
            if (orders.buy.has_value())
                stock->placeOrder(cfg.pairsymb,0,0,magic,orders.buy->id,0);
            if (orders.sell.has_value())
                stock->placeOrder(cfg.pairsymb,0,0,magic,orders.sell->id,0);
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
            Order buyorder(0, 0, IStrategy::Alert::disabled, AlertReason::unknown);
            Order sellorder(0, 0, IStrategy::Alert::disabled, AlertReason::unknown);
            ISpreadGen::Result sugg_orders = cfg.spread->get_result(spread_state, centerPrice);

            if (grant_trade) {
                buyorder = calcBuyOrderSize(status, status.curPrice*3, centerPrice, false);
                sellorder = calcSellOrderSize(status, 0, centerPrice, false);
            } else {
                if (sugg_orders.buy.has_value()) {
                    buyorder = calcBuyOrderSize(status, *sugg_orders.buy, centerPrice, need_alerts);
                }
                if (sugg_orders.sell.has_value()) {
                    sellorder = calcSellOrderSize(status, *sugg_orders.sell, centerPrice, need_alerts);
                }
            }
            //set target slightly below requested order to avoid rounding errors
            //use half of asset step as threshold
            target_buy_size = buyorder.size-minfo.asset_step*0.5;
            //set target slightly above requested order to avoid rounding errors
            //use half of asset step as threshold
            target_sell_size = sellorder.size+minfo.asset_step*0.5;

            if (!buyorder.size || !sellorder.size) {
                flush_partial(status);
            } else if (buyorder.size - partial_position < minfo.asset_step) {
                flush_partial(status);
                buyorder.size = 0;
                buyorder.alert = IStrategy::Alert::disabled;
            } else if (sellorder.size - partial_position > -minfo.asset_step) {
                flush_partial(status);
                sellorder.size = 0;
                sellorder.alert = IStrategy::Alert::disabled;
            } else {
                buyorder.size = std::min(buyorder.size, buyorder.size - partial_position);
                sellorder.size = std::max(sellorder.size, sellorder.size - partial_position);
                buyorder.size = std::max(buyorder.size, minfo.calcMinSize(buyorder.price));
                sellorder.size = std::min(sellorder.size, -minfo.calcMinSize(sellorder.price));
            }

            if (first_cycle || anytrades) {
                sellorder.size = 0;sellorder.alert = IStrategy::Alert::disabled;
                buyorder.size = 0;buyorder.alert = IStrategy::Alert::disabled;
            }

            try {
                setOrder(orders.buy, buyorder, buy_alert, false);
            } catch (std::exception &e) {
                buy_order_error = e.what();
            }
            try {
                setOrder(orders.sell, sellorder, sell_alert, false);
            } catch (std::exception &e) {
                sell_order_error = e.what();
            }

            if (buy_alert.has_value() && sell_alert.has_value() && buy_alert->price > sell_alert->price) {
                std::swap(buy_alert, sell_alert);
            }

            //report order errors to UI
            if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj(buy_order_error, sell_order_error));

        }


		Strategy buy_state = strategy;
		Strategy sell_state = strategy;
		double buy_norm = 0;
		double sell_norm = 0;
		if (!cfg.hidden || cfg.secondary_order_distance>0) {
			if (sell_alert.has_value()) {
				sell_norm = sell_state.onTrade(minfo, sell_alert->price, 0, position, status.currencyBalance).normProfit;
			} else if (orders.sell.has_value()) {
				sell_norm = sell_state.onTrade(minfo, orders.sell->price, orders.sell->size, position+orders.sell->size, status.currencyBalance).normProfit;
			}
			if (buy_alert.has_value()) {
				buy_norm = buy_state.onTrade(minfo, buy_alert->price, 0, position, status.currencyBalance).normProfit;
			} else if (orders.buy.has_value()) {
				buy_norm = buy_state.onTrade(minfo, orders.buy->price, orders.buy->size, position+orders.buy->size, status.currencyBalance).normProfit;
			}
		}


		if (!cfg.hidden) {
			int last_trade_dir = !anytrades?0:sgn(lastTradeSize);
            if (last_trade_dir < 0) orders.sell.reset();
            if (last_trade_dir > 0) orders.buy.reset();
            auto minmax = strategy.calcSafeRange(minfo,status.assetAvailBalance,status.currencyAvailBalance);
            auto budget = strategy.getBudgetInfo();
			//report orders to UI
			statsvc->reportOrders(1,orders.buy,orders.sell);
			//report trades to UI
			statsvc->reportTrades({position,minfo.invert_price,budget.total}, trades);
			//report price to UI
			statsvc->reportPrice(status.ticker.last);
			//report misc
			std::optional<double> budget_extra;
			if (!trades.empty())
			{
//				double last_price = trades.back().eff_price;
				double locked = wcfg.walletDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid)).otherTraders;
				double currency = strategy.calcCurrencyAllocation(status.curPrice, minfo.leverage>0);
				budget_extra =  status.currencyUnadjustedBalance - locked - currency;
			}

			auto spread_stat = cfg.spread->get_stats(spread_state, centerPrice  );

			statsvc->reportMisc(IStatSvc::MiscData{
				last_trade_dir,
				achieve_mode,
				cfg.enabled,
				trade_now_mode,
				eq,
				spread_stat.spread,
				spread_stat.mult_buy,
				spread_stat.mult_sell,
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
				    unsigned int max_count = std::max<unsigned int>(cfg.spread->get_required_history_length(), 240*60);
					//delete very old data from chart
					if (chart.size() > max_count)
						chart.erase(chart.begin(),chart.end()-max_count);
				}
				cfg.spread->point(spread_state, status.curPrice, false);
			}
		}

		lastTradeId  = status.new_trades.lastId;




		//save state
		saveState();
		first_cycle = false;

	} catch (std::exception &e) {
		if (!cfg.hidden) {
			statsvc->reportTrades({position,false,0},trades);
			std::string error;
			error.append(e.what());
			statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
			throw;
		}
	}
}


MTrader::OrderPair MTrader::getOrders() {
	OrderPair ret;
	auto data = stock->getOpenOrders(cfg.pairsymb);
	for (auto &&x: data) {
		try {
			if (x.client_id == magic) {
				IStockApi::Order o(x);
				if (o.size<0) {
					if (ret.sell.has_value()) {
						ondra_shared::logWarning("Multiple sell orders (trying to cancel)");
						stock->placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.sell = o;
					}
				} else {
					if (ret.buy.has_value()) {
						ondra_shared::logWarning("Multiple buy orders (trying to cancel)");
						stock->placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.buy = o;
					}
				}
			}
		} catch (std::exception &e) {
			ondra_shared::logError("$1", e.what());
		}
	}
	return ret;
}


void MTrader::setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<AlertInfo> &alert, bool secondary) {
	alert.reset();
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
}


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

// merge trades here
	auto new_trades = stock->syncTrades(lastTradeId, cfg.pairsymb);
	res.new_trades.lastId = new_trades.lastId;
	for (auto &&k : new_trades.trades) {
		if (last_trade->price == k.price) {
			last_trade->eff_price = (last_trade->eff_price * last_trade->size + k.eff_price*k.size)/(last_trade->size+k.size);
			last_trade->size += k.size;
			last_trade->eff_size += k.eff_size;
			last_trade->time = k.time;
		} else {
			res.new_trades.trades.push_back(k);
			last_trade = &res.new_trades.trades.back();
		}
	}

	res.brokerAssetBalance= stock->getBalance(minfo.asset_symbol, cfg.pairsymb);
	res.brokerCurrencyBalance = stock->getBalance(minfo.currency_symbol, cfg.pairsymb);
	res.currencyUnadjustedBalance = *res.brokerCurrencyBalance + wcfg.externalBalance.lock_shared()->get(cfg.broker, minfo.wallet_id, minfo.currency_symbol);
	auto wdb = wcfg.walletDB.lock_shared();
	if (minfo.leverage == 0) {
	    res.assetUnadjustedBalance = *res.brokerAssetBalance +  wcfg.externalBalance.lock_shared()->get(cfg.broker, minfo.wallet_id, minfo.asset_symbol);
		res.assetBalance = wdb->adjAssets(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),res.assetUnadjustedBalance);
		res.assetAvailBalance = wdb->adjAssets(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),*res.brokerAssetBalance);
	} else {
	    res.assetUnadjustedBalance = *res.brokerAssetBalance;
		res.assetBalance = *res.brokerAssetBalance;
		res.assetAvailBalance = res.assetBalance;
	}
	res.currencyBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),	res.currencyUnadjustedBalance);
	res.currencyAvailBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),*res.brokerCurrencyBalance);


	auto ticker = stock->getTicker(cfg.pairsymb);
	res.ticker = ticker;
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);

	if (ticker.bid < 0 || ticker.ask < 0 || ticker.bid > ticker.ask)
		throw std::runtime_error("Broker error: Ticker invalid values");

	res.chartItem.time = ticker.time;
	res.chartItem.bid = ticker.bid;
	res.chartItem.ask = ticker.ask;
	res.chartItem.last = ticker.last;


	return res;
}

bool MTrader::calculateOrderFeeLessAdjust(Order &order, double position, double currency, int dir, bool alerts, double asset_fees, bool no_leverage_check) const {

    order.size /= asset_fees;

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


    constexpr auto adjust_assets = [](double v) {
        if (v < 0) return std::ceil(v);
        else return std::floor(v);
    };

    //if size of order is above max_size, adjust to max_size
    if (cfg.max_size && std::fabs(order.size) > cfg.max_size) {
        order.size = cfg.max_size*dir;
    }

    order.size = minfo.adjValue(order.size, minfo.asset_step, adjust_assets);

    //if order size is below min_size, adjust to zero
    if (std::fabs(order.size) < minfo.calcMinSize(order.price)) order.size = 0;

    order.size = minfo.adjValue(order.size, minfo.asset_step, adjust_assets);

    if (order.size == 0) {
        order.ar = AlertReason::below_minsize;
        //in this case, we continue to search better price (don't accept the order)
        return false;
    }


    //check leverage
    double d;
    auto chkres = checkLeverage(order, position, currency, d);
    if (chkres != AlertReason::unknown)  {
        //adjust order when leverage reached
        order.size = d;
        //if result is zero
        if (d == 0 || no_leverage_check) {
            //force alert
            order.alert = IStrategy::Alert::forced;
            order.ar = chkres;
            return true;
        } else {
            return calculateOrderFeeLessAdjust(order, position, currency, dir, alerts, 1.0, true);
        }
    }


    return true;



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
			position_valid = state["internal_balance"].hasValue();
			if (position_valid) {
				position = state["internal_balance"].getNumber();
			} else {
				position = 0;
			}
			if (state["currency_balance"].hasValue()) {currency = state["currency_balance"].getNumber(); currency_valid = true;}
			json::Value accval = state["account_value"];
			std::size_t nuid = state["uid"].getUInt();
			if (nuid) uid = nuid;
			lastTradeId = state["lastTradeId"];
			lastPriceOffset = state["lastPriceOffset"].getNumber();
			lastTradePrice = state["lastTradePrice"].getNumber();
			trade_now_mode = state["trade_now_mode"].getBool();

			achieve_mode = state["achieve_mode"].getBool();
			need_initial_reset = state["need_initial_reset"].getBool();
			adj_wait = state["adj_wait"].getUInt();
			adj_wait_price = state["adj_wait_price"].getNumber();
			accumulated = state["accumulated"].getNumber();
			auto partial = state["partial"];
			if (partial.defined()) {
			    partial_eff_pos = ACB(partial[1].getNumber(), partial[0].getNumber(),0.0);
			    partial_position = partial[2].getNumber();
			}
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
	initializeSpread();


}

void MTrader::saveState() {
	if (storage == nullptr || need_load) return;
	json::Object obj;

	{
		auto st = obj.object("state");
		if (position_valid)
			st.set("internal_balance", position);
		if (currency_valid)
			st.set("currency_balance", currency);
		st.set("uid",uid);
		st.set("lastTradeId",lastTradeId);
		st.set("lastPriceOffset",lastPriceOffset);
		st.set("lastTradePrice", lastTradePrice);
		st.set("private_chart", minfo.private_chart||minfo.simulator);
		st.set("accumulated",accumulated);
		st.set("trade_now_mode", trade_now_mode);
		if (achieve_mode) st.set("achieve_mode", achieve_mode);
		if (need_initial_reset) st.set("need_initial_reset", need_initial_reset);
		st.set("adj_wait",adj_wait);
		if (adj_wait) st.set("adj_wait_price", adj_wait_price);
		if (partial_eff_pos.getPos()) {
		    st.set("partial", {partial_eff_pos.getPos(),partial_eff_pos.getOpen(), partial_position});
		}
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
	auto iter = std::remove_if(st.new_trades.trades.begin(), st.new_trades.trades.end(),
			[&](const IStockApi::Trade &t) {
				return std::find_if(trades.begin(), trades.end(),[&](const IStockApi::Trade &q) {
					return t.id == q.id;
				}) != trades.end();
	});

	st.new_trades.trades.erase(iter, st.new_trades.trades.end());


	double assetBal = position_valid?position:st.assetBalance;
	double curBal = st.currencyBalance - (minfo.leverage?0:std::accumulate(
			st.new_trades.trades.begin(), st.new_trades.trades.end(),0,[](double a, const IStockApi::Trade &tr){
				return a - tr.price*tr.size;
			}));


	double last_np = 0;
	double last_ap = 0;
	double last_price = 0;
	double last_neutral = 0;
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;
		last_price = trades.back().eff_price;
		last_neutral = trades.back().neutral_price;
	}

	bool res = false;

	for (auto &&t : st.new_trades.trades) {
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

		double norm_adv = 0;

		partial_eff_pos = partial_eff_pos(t.eff_price, t.eff_size);
		partial_position += t.size;
		if (partial_eff_pos.getRPnL()) {
		    norm_adv = partial_eff_pos.getRPnL();
		    partial_eff_pos = partial_eff_pos.resetRPnL();
		    logDebug("(PARTIAL) Added profit: $1", norm_adv);
		}
        logDebug("(PARTIAL) Trade partial: price=$1, size=$2, final_price=$3, final_size=$4", t.eff_price, t.eff_size,  partial_eff_pos.getOpen(),partial_eff_pos.getPos());
		bool manual = achieve_mode || !cfg.enabled;
		trades.push_back(TWBItem(t, last_np+norm_adv, last_ap, last_neutral, !manual, manual));

		if (partial_position > target_buy_size
		        || partial_position < target_sell_size) {
		    position = assetBal;
		    flush_partial(st);
		}

	}
	wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(last_price, minfo.leverage>0));

	if (position_valid) position = assetBal;
	else position = st.assetBalance;
	position_valid = true;

	return res;
}

void MTrader::flush_partial(const Status &status) {
    double trade_size = partial_eff_pos.getPos();
    if (trade_size) {
        double trade_price = partial_eff_pos.getOpen();
        auto tstate = strategy.onTrade(minfo, trade_price, trade_size, position, currency);
        if (!trades.empty()) {
            auto &t = trades.back();
            t.eff_size-=tstate.normAccum;
            position -=tstate.normAccum;
            accumulated +=tstate.normAccum;
            t.norm_profit+=tstate.normProfit;
            t.norm_accum+=tstate.normAccum;
            t.neutral_price = tstate.neutralPrice;
            t.partial_exec = false;
        }

        cfg.spread->point(spread_state, trades.back().price, true);
        lastTradePrice = trade_price;
        logDebug("(PARTIAL) Trade commit to strategy: price=$1, size=$2, norm_profit=$3", trade_price, trade_size, tstate.normProfit);
        partial_eff_pos = ACB(0,0);
        partial_position = 0;
        trade_now_mode = false;
    }
}



void MTrader::clearStats() {
	init();
	trades.clear();
	position = 0;
	position_valid = false;
	adj_wait = 0;
	adj_wait_price = 0;
	accumulated = 0;
	spread_state = cfg.spread->start();
	saveState();
	updateEnterPrice();
}

void MTrader::stop() {
	cfg.enabled = false;
}

void MTrader::reset(const ResetOptions &ropt) {
	init();
	stock->reset(std::chrono::system_clock::now());
	//check whether lastTradeId is unable to retrieve trades
	auto syncState = stock->syncTrades(lastTradeId, cfg.pairsymb);
	//if no trades are received, we can freely reset lastTradeId;
	if (syncState.trades.empty()) {
		//so obtain new lastTradeId;
		syncState = stock->syncTrades(nullptr, cfg.pairsymb);
		//store it
		lastTradeId = syncState.lastId;
	}
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
	} else if (minfo.leverage){
	    position = assets = stock->getBalance(minfo.asset_symbol, cfg.pairsymb);
	    accumulated = 0;
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

MTrader::Chart MTrader::getChart() const {
	return chart;
}


void MTrader::addAcceptLossAlert() {
	Status st = getMarketStatus();
	alertTrigger(st, st.ticker.last, 0, AlertReason::initial_reset);
}


void MTrader::dropState() {
	storage->erase();
}



std::optional<double> MTrader::getPosition() const {
	return position;
}

double MTrader::getPartialPosition() const {
    return partial_position;
}

std::optional<double> MTrader::getCurrency() const {
	return currency;
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
    return checkEquilibriumClose(st, st.curPrice);
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
    auto orders = cfg.spread->get_result(spread_state, eq);
    if (orders.buy.has_value() && orders.sell.has_value()) {
        double low = *orders.buy;
        double hi = *orders.sell;
        return lastTradePrice >= low && lastTradePrice <=hi;
    } else {
        return false;
    }

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
	} else if (minfo.leverage == 0) {
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
	},0,accum,0,false, true));
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

json::Value MTrader::getOHLC(std::uint64_t interval) const {
    json::Array out;
    interval *= 60000; //60milliseconds
    std::uint64_t tm = 0;
    double last = 0;
    double ohlc[4];

    for (const auto &item : chart) {
        double v = item.last;
        if (minfo.invert_price) v = 1.0/v;
        std::size_t x = static_cast<std::size_t>(item.time/interval);
        if (tm != x) {
            if (tm) {
                out.push_back({tm*interval,{ohlc[0],ohlc[1],ohlc[2],ohlc[3]}});
            }
            tm = x;
            ohlc[3] = v;
            ohlc[0] = last?last:v;
            ohlc[2] = std::min(ohlc[0], v);
            ohlc[1] = std::max(ohlc[0], v);
            last = v;
        } else {
            ohlc[3] = v;
            ohlc[2] = std::min(ohlc[2], v);
            ohlc[1] = std::max(ohlc[1], v);
            last = v;
        }
    }
    if (tm) {
        out.push_back({tm*interval,{ohlc[0],ohlc[1],ohlc[2],ohlc[3]}});
    }
    return out;
}

void MTrader::initializeSpread() {
    spread_state = cfg.spread->start();
    auto chart_iter = chart.begin();
    auto chart_end = chart.end();
    auto trades_iter = trades.begin();
    auto trades_end = trades.end();

    if (chart_iter != chart_end) {
        while (trades_iter != trades_end && trades_iter->time < chart_iter->time) {
            ++trades_iter;
        }
        cfg.spread->point(spread_state, chart_iter->last, false);
        ++chart_iter;
        while (chart_iter != chart_end) {
            while (trades_iter != trades_end && trades_iter->time < chart_iter->time) {
                if (!trades_iter->partial_exec) {
                    cfg.spread->point(spread_state, trades_iter->price, true);
                }
                ++trades_iter;
            }

            cfg.spread->point(spread_state, chart_iter->last, false);
            ++chart_iter;
        }
    }
}

MTrader::Order MTrader::calcBuyOrderSize(const Status &status, double base, double center, bool enable_alerts) const {

    double c = minfo.addFees(center - minfo.currency_step, 1).adjusted_price;
    if (c < base) base = c;

    double ask_level = status.ticker.ask;
    auto ask_tick = minfo.priceToTick(ask_level);
    auto ord_tick = minfo.priceToTickDown(base);
    if (ask_tick-1 < ord_tick) {
        ord_tick = ask_tick-1;
        base = minfo.tickToPrice(ord_tick);
    }
    bool rej = false;
    for (double i = 1.0; i > 0.5; i-=0.01) {
        double base_price = base * i;
        auto fees = minfo.removeFees(base_price, 1);
        Order ord (strategy.getNewOrder(minfo, ask_level, fees.adjusted_price, 1, status.assetBalance, status.currencyBalance, rej),
                    AlertReason::strategy_enforced);
        if (ord.price <= 0) ord.price = base_price;
        else ord.price = minfo.addFees(ord.price,1).adjusted_price;
        ord_tick = minfo.priceToTickDown(ord.price);
        if (ask_tick-1 >= ord_tick) {
            if (calculateOrderFeeLessAdjust(ord, status.assetBalance, status.currencyBalance, 1, enable_alerts, fees.asset_multiplier)) {
                ord.price = minfo.tickToPrice(ord_tick);
                return calcOrderTrailer(ord, base);
            }
        }
    }
    return Order(0, base, IStrategy::Alert::forced, AlertReason::below_minsize);
}
MTrader::Order MTrader::calcSellOrderSize(const Status &status, double base, double center, bool enable_alerts) const {

    double c = minfo.addFees(center + minfo.currency_step, -1).adjusted_price;
    if (c > base) base = c;

    double bid_level = status.ticker.bid;
    auto bid_tick = minfo.priceToTick(bid_level);
    auto ord_tick = minfo.priceToTickUp(base);
    if (bid_tick+1 > ord_tick) {
        ord_tick = bid_tick+1;
        base = minfo.tickToPrice(ord_tick);
    }
    bool rej = false;
    for (double i = 1.0; i < 2.0; i+=0.01) {
        double base_price = base * i;
        auto fees = minfo.removeFees(base_price, -1);
        Order ord (strategy.getNewOrder(minfo, bid_level, fees.adjusted_price, -1, status.assetBalance, status.currencyBalance, rej),
                    AlertReason::strategy_enforced);
        if (ord.price <= 0) ord.price = base_price;
        else ord.price = minfo.addFees(ord.price,-1).adjusted_price;
        ord_tick = minfo.priceToTickUp(ord.price);
        if (bid_tick+1 <= ord_tick) {
            if (calculateOrderFeeLessAdjust(ord, status.assetBalance, status.currencyBalance, -1, enable_alerts, fees.asset_multiplier)) {
                ord.price = minfo.tickToPrice(ord_tick);
                return calcOrderTrailer(ord, base);
            }
        }
    }
    return Order(0, base, IStrategy::Alert::forced, AlertReason::below_minsize);

}

MTrader::Order MTrader::calcOrderTrailer(Order order, double origPrice) const {
    auto lmsz = limitOrderMinMaxBalance(position, order.size, order.price);
    if (lmsz.first != AlertReason::unknown) {
        order.size = lmsz.second;
        if (!order.size) {
            order.alert = IStrategy::Alert::forced;
        }
        order.ar = lmsz.first;
    }
    if (order.size == 0 && order.alert != IStrategy::Alert::disabled) {
        order.price = origPrice;
        order.alert = IStrategy::Alert::forced;
    }
    return order;


}
