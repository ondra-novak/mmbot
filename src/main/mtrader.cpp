#include <imtjson/value.h>
#include <imtjson/string.h>
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
#include "emulatedLeverageBroker.h"
#include "emulator.h"
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




void MTrader_Config::loadConfig(json::Value data, bool force_dry_run) {
	pairsymb = data["pair_symbol"].getString();
	broker = data["broker"].getString();
	title = data["title"].getString();

	auto strdata = data["strategy"];
	auto strstr = strdata["type"].toString();
	strategy = Strategy::create(strstr.str(), strdata);


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

	dry_run = force_dry_run || data["dry_run"].getValueOrDefault(false);
	internal_balance = data["internal_balance"].getValueOrDefault(false) || dry_run;
	dont_allocate = data["dont_allocate"].getValueOrDefault(false) ;
	enabled= data["enabled"].getValueOrDefault(true);
	hidden = data["hidden"].getValueOrDefault(false);
	dynmult_sliding = data["dynmult_sliding"].getValueOrDefault(false);
	dynmult_mult = data["dynmult_mult"].getValueOrDefault(false);
	swap_symbols= data["swap_symbols"].getValueOrDefault(false);
	emulate_leveraged=data["emulate_leveraged"].getValueOrDefault(0.0);
	reduce_on_leverage=data["reduce_on_leverage"].getBool();
	freeze_spread=data["spread_freeze"].getBool();
	trade_within_budget = data["trade_within_budget"].getBool();
	init_open = data["init_open"].getNumber();

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
:stock(selectStock(stock_selector,config))
,cfg(config)
,storage(std::move(storage))
,statsvc(std::move(statsvc))
,wcfg(walletCfg)
,strategy(config.strategy)
,dynmult({cfg.dynmult_raise,cfg.dynmult_fall, cfg.dynmult_cap, cfg.dynmult_mode, cfg.dynmult_mult})
,spread_fn(defaultSpreadFunction(cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours, cfg.force_spread))
{
	//probe that broker is valid configured
	stock->testBroker();
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


PStockApi MTrader::selectStock(IStockSelector &stock_selector, const Config &conf) {
	PStockApi s = stock_selector.getStock(conf.broker);
	if (s == nullptr) throw std::runtime_error(std::string("Unknown stock market name: ")+std::string(conf.broker));
	if (conf.swap_symbols) {
		s = std::make_shared<SwapBroker>(s);
	}
	if (conf.emulate_leveraged>0) {
		s = std::make_shared<EmulatedLeverageBroker>(s,conf.emulate_leveraged);
	}
	if (conf.dry_run) {
		auto new_s = std::make_shared<EmulatorAPI>(s, 0);
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

void MTrader::alertTrigger(Status &st, double price) {
	st.new_trades.trades.push_back(IStockApi::Trade{
		json::Value(json::String({"ALERT:",json::Value(st.chartItem.time).toString()})),
		st.chartItem.time,
		0,
		price,
		0,
		price
	});
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
}

void MTrader::perform(bool manually) {

	try {
		init();


		//Get opened orders
		auto orders = getOrders();
		//get current status
		auto status = getMarketStatus();

		if (status.brokerCurrencyBalance.has_value()) {
			wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.currency_symbol, *status.brokerCurrencyBalance);
		}
		if (!cfg.internal_balance && minfo.leverage == 0 && position_valid) {
			double accum = getAccumulated();
			if (status.brokerAssetBalance.has_value()) wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.asset_symbol, *status.brokerAssetBalance);
			wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position+accum);
			wcfg.accumDB.lock()->alloc(getWalletAssetKey(), accum);
		}

		double eq = strategy.getEquilibrium(status.assetBalance);
		bool delayed_trade_detect = false;
		std::string buy_order_error;
		std::string sell_order_error;
		//update market fees
		minfo.fees = status.new_fees;
		//process all new trades
		bool anytrades = processTrades(status);

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

		//fast_trade is true, when there is missing order after execution
		//which assumes, that complete execution achieved
		//orderwise it is false which can mean, that only partial execution achieved
		bool fast_trade = false;

		if (anytrades && ((!orders.buy.has_value() && !buy_alert.has_value())
				|| (!orders.sell.has_value() && !sell_alert.has_value()))) {
			recalc = true;
			fast_trade = true;
			sell_alert.reset();
			buy_alert.reset();
		}

		bool need_alerts = trades.empty()?false:trades.back().size == 0;

		double lastTradeSize = trades.empty()?0:trades.back().eff_size;
		if (lastTradePrice == 0 ) {
			lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium(status.assetBalance):status.curPrice;
			if (!std::isfinite(lastTradePrice)) lastTradePrice = status.curPrice;		}


		if (cfg.max_size != 0) need_alerts = true;
		double centerPrice = need_alerts?lastTradePrice:strategy.getCenterPrice(lastTradePrice, status.assetBalance);

		if (cfg.dynmult_sliding) {
			centerPrice = (centerPrice-lastTradePrice)+lastPriceOffset+status.spreadCenter;
		}


		//only create orders, if there are no trades from previous run
		if (!anytrades || fast_trade) {

			bool grant_trade = cfg.grant_trade_minutes
					&& !trades.empty()
					&& status.ticker.time > trades.back().time
					&& (status.ticker.time - trades.back().time)/60000 > cfg.grant_trade_minutes;


			if (achieve_mode) {
				achieve_mode = !checkAchieveModeDone(status);
				grant_trade |= achieve_mode;
			}

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
					if (cfg.freeze_spread) {
						logProgress("Spread frozen: dir=$1, value=$2", frozen_spread_side, frozen_spread);
					}
				}
			}

			if (grant_trade) {
				dynmult.reset();
			}


			if (status.new_trades.trades.empty()) {
				//process alerts
				if (sell_alert.has_value() && status.ticker.last >= *sell_alert) {
					alertTrigger(status, *sell_alert);
					lastTradePrice=*sell_alert;
					update_dynmult(false,true);
				}
				if (buy_alert.has_value() && status.ticker.last <= *buy_alert) {
					alertTrigger(status, *buy_alert);
					lastTradePrice=*buy_alert;
					update_dynmult(true,false);
				}
				if (!status.new_trades.trades.empty()) {
					processTrades(status);
				}
			}


			strategy.onIdle(minfo, status.ticker, status.assetBalance, status.currencyBalance);

			if (status.curStep) {

				if (!cfg.enabled || need_initial_reset || delayed_trade_detect)  {
					if (orders.buy.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic,orders.buy->id,0);
					if (orders.sell.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic,orders.sell->id,0);
					if (orders.buy2.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic2,orders.buy2->id,0);
					if (orders.sell2.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic2,orders.sell2->id,0);
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
					Order buyorder;
					Order sellorder;
					double maxPosition;
					if (need_alerts && checkReduceOnLeverage(status, maxPosition)) {
						double diff = maxPosition - status.assetBalance;
						if (diff < 0) {
							sellorder = Order(diff, status.ticker.ask, IStrategy::Alert::disabled);
							buyorder = Order(0, status.ticker.ask/2, IStrategy::Alert::disabled);
						} else {
							buyorder = Order(diff, status.ticker.bid, IStrategy::Alert::disabled);
							sellorder = Order(0, status.ticker.bid*2, IStrategy::Alert::disabled);
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
						hspread = std::max(hspread,1e-10); //spread can't be zero, so put there small number
						lspread = std::max(lspread,1e-10);
							//calculate buy order
						buyorder = calculateOrder(strategy,centerPrice,-lspread,
								dynmult.getBuyMult(),status.ticker.bid,
								position,status.currencyAvailBalance,need_alerts);
							//calculate sell order
						sellorder = calculateOrder(strategy,centerPrice,hspread,
								dynmult.getSellMult(),status.ticker.ask,
								position,status.currencyAvailBalance,need_alerts);

						if (cfg.dynmult_sliding && !need_alerts) {
							double bp = centerPrice*std::exp(-lspread);
							double sp = centerPrice*std::exp(hspread);
							if (status.ticker.bid > sp) {
								auto b2 = calculateOrder(strategy,lastTradePrice,-lspread,
										1.0,status.ticker.bid,
										position,status.currencyAvailBalance,true);
								if (b2.size) buyorder = b2;
							}
							if (status.ticker.ask < bp) {
								auto s2 = calculateOrder(strategy,lastTradePrice,hspread,
										1.0,status.ticker.ask,
										position,status.currencyAvailBalance,true);
								if (s2.size) sellorder = s2;

							}
						}



					}

					/*
					if (grant_trade) {
						sellorder.alert = IStrategy::Alert::disabled;
						buyorder.alert = IStrategy::Alert::disabled;
						if (status.curPrice < eq) {
							sellorder.size = 0;
						} else if (status.curPrice > eq) {
							buyorder.size = 0;
						}
					}*/

					for (int _i=0;_i<2;_i++) {
						try {
							setOrder(orders.buy, buyorder, buy_alert, false);
							if (!orders.buy.has_value()) {
								acceptLoss(status, 1);
							}
							_i=1;
						} catch (std::exception &e) {
							if (!orders.buy2.has_value() || _i) {
								buy_order_error = e.what();
								acceptLoss(status, 1);
								_i=1;
							} else {
								stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.buy2->id, 0);
								orders.buy2.reset();
								logProgress("Canceled 2nd buy order because error $1",e.what());
							}
						}
					}
					for (int _i=0;_i<2;_i++) {
						try {
							setOrder(orders.sell, sellorder, sell_alert, false);
							if (!orders.sell.has_value()) {
								acceptLoss(status, -1);
							}
							_i=1;
						} catch (std::exception &e) {
							if (!orders.sell2.has_value() || _i) {
 								sell_order_error = e.what();
								acceptLoss(status,-1);
								_i=1;
							} else {
								stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.sell2->id, 0);
								orders.sell2.reset();
								logProgress("Canceled 2nd sell order because error $1",e.what());
							}
						}
					}

					if (buy_alert.has_value() && sell_alert.has_value() && *buy_alert > *sell_alert) {
						std::swap(buy_alert, sell_alert);
					}

					if (!recalc && !manually) {
						update_dynmult(false,false);
					}

					//report order errors to UI
					if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj(buy_order_error, sell_order_error));

				}
			} else {
				if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj("Initializing, please wait\n(5 minutes aprox.)"));
			}

			recalc = false;

		} else {

			recalc = true;
			sell_alert.reset();
			buy_alert.reset();
		}

		Strategy buy_state = strategy;
		Strategy sell_state = strategy;
		double buy_norm = 0;
		double sell_norm = 0;
		if (!cfg.hidden || cfg.secondary_order_distance>0) {
			if (sell_alert.has_value()) {
				sell_norm = sell_state.onTrade(minfo, *sell_alert, 0, position, status.currencyBalance).normProfit;
			} else if (orders.sell.has_value()) {
				sell_norm = sell_state.onTrade(minfo, orders.sell->price, orders.sell->size, position+orders.sell->size, status.currencyBalance).normProfit;
			}
			if (buy_alert.has_value()) {
				buy_norm = buy_state.onTrade(minfo, *buy_alert, 0, position, status.currencyBalance).normProfit;
			} else if (orders.buy.has_value()) {
				buy_norm = buy_state.onTrade(minfo, orders.buy->price, orders.buy->size, position+orders.buy->size, status.currencyBalance).normProfit;
			}
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
				double last_price = trades.back().eff_price;
				double locked = cfg.internal_balance?0:wcfg.walletDB.lock_shared()->query(WalletDB::KeyQuery(cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid)).otherTraders;
				double budget = strategy.calcCurrencyAllocation(last_price);
				if (budget) {
					budget_extra =  status.currencyUnadjustedBalance - locked - budget;
				}
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
				enter_price_sum/enter_price_pos,
				enter_price_pnl,
				status.curPrice*enter_price_pos - enter_price_sum
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

		lastTradeId  = status.new_trades.lastId;

		if (cfg.secondary_order_distance > 0 && orders.buy.has_value() && orders.buy->price > 0) {
			std::optional<double> alert;
			try {
				auto buyorder = calculateOrder(buy_state,orders.buy->price,-status.curStep,
												cfg.secondary_order_distance,
												status.ticker.bid,position+orders.buy->size,
												status.currencyAvailBalance,false);
				setOrder(orders.buy2, buyorder, alert, true);
			} catch (std::exception &e) {
				logError("Failed to create secondary order: $1", e.what());
			}
		} else if (orders.buy2.has_value()) {
			stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.buy2->id, 0);
			orders.buy2.reset();
		}


		if (cfg.secondary_order_distance > 0 && orders.sell.has_value() && orders.sell->price > 0) {
			std::optional<double> alert;
			try {
				auto sellorder = calculateOrder(sell_state,orders.sell->price, status.curStep,
													 cfg.secondary_order_distance,
													 status.ticker.ask, position+orders.sell->size,
													 status.currencyAvailBalance,false);
				setOrder(orders.sell2, sellorder, alert, true);
			} catch (std::exception &e) {
				logError("Failed to create secondary order: $1", e.what());
			}
		} else 	if (orders.sell2.has_value()) {
			stock->placeOrder(cfg.pairsymb, 0, 0, json::Value(), orders.sell2->id, 0);
			orders.sell2.reset();
		}
		statsvc->reportOrders(2,orders.buy2,orders.sell2);



		//save state
		saveState();
		first_cycle = false;

	} catch (std::exception &e) {
		statsvc->reportTrades(position,trades);
		std::string error;
		error.append(e.what());
		statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
		statsvc->reportMisc(IStatSvc::MiscData{
			0,false,cfg.enabled,0,0,dynmult.getBuyMult(),dynmult.getSellMult(),0,0,0,0,accumulated,0,
			trades.size(),trades.empty()?0UL:(trades.back().time-trades[0].time),lastTradePrice,position,
					0,0,enter_price_sum/enter_price_pos,enter_price_pnl,
					lastTradePrice*enter_price_pos-enter_price_sum*enter_price_pos
		},true);
		statsvc->reportPrice(trades.empty()?1:trades.back().price);
		throw;
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
			if (x.client_id == magic2) {
				IStockApi::Order o(x);
				if (o.size<0) {
					if (ret.sell2.has_value()) {
						ondra_shared::logWarning("Multiple secondary sell orders (trying to cancel)");
						stock->placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.sell2 = o;
					}
				} else {
					if (ret.buy2.has_value()) {
						ondra_shared::logWarning("Multiple secondary buy orders (trying to cancel)");
						stock->placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.buy2 = o;
					}
				}
			}
		} catch (std::exception &e) {
			ondra_shared::logError("$1", e.what());
		}
	}
	return ret;
}


void MTrader::setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<double> &alert, bool secondary) {
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
				stock->placeOrder(cfg.pairsymb,0,0,nullptr,orig->id,0);
			}
			alert = neworder.price;
			neworder.size = 0;
			neworder.update(orig);
			return;
		}

		if (neworder.size == 0) {
			if (neworder.alert == IStrategy::Alert::disabled|| orig.has_value()) return;
			alert = neworder.price;
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
				alert = neworder.price;
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

//handle option internal_balance in case, that we have internal balance data
	if (cfg.internal_balance && currency_valid && position_valid) {
		auto sumt = std::accumulate(res.new_trades.trades.begin(),
				res.new_trades.trades.end(),std::pair<double,double>(0,0),sumTrades<IStockApi::Trade>);
		res.assetBalance = sumt.first;
		res.currencyBalance = sumt.second;
		if (minfo.leverage) res.currencyBalance = 0;
		res.assetBalance += position;
		res.currencyBalance += currency;;
		res.currencyUnadjustedBalance = res.currencyBalance;
		res.assetUnadjustedBalance = res.assetBalance;
		res.assetAvailBalance = res.assetBalance;
		res.currencyAvailBalance = res.currencyBalance;
	} else {
		res.brokerAssetBalance= stock->getBalance(minfo.asset_symbol, cfg.pairsymb);
		res.brokerCurrencyBalance = stock->getBalance(minfo.currency_symbol, cfg.pairsymb);
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
	}

	res.new_fees = stock->getFees(cfg.pairsymb);

	auto ticker = stock->getTicker(cfg.pairsymb);
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


	return res;
}

bool MTrader::calculateOrderFeeLessAdjust(Order &order, double assets, double currency, int dir, bool alerts, double min_size) const {

	//order is reversed to requested direction
	if (order.size * dir < 0) {
		//we can't trade this, so assume, that order size is zero
		order.size = 0;
	}

	if (order.size == 0) {
		//for forced, stoploss, disabled, we accept current order
		//otherwise it depends on "alerts enabled"
		if (order.alert != IStrategy::Alert::enabled) return true;
		else return alerts;
	}

	//check leverage
	double d;
	if (!checkLeverage(order, assets, currency, d))  {
		//adjust order when leverage reached
		order.size = d;
		//if result is zero
		if (d == 0) {
			//force alert
			order.alert = IStrategy::Alert::forced;
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

	//in this case, we continue to search better price (don't accept the order)
	return order.size !=0 ;

}

MTrader::Order MTrader::calculateOrderFeeLess(
		Strategy state,
		double prevPrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
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



	order= state.getNewOrder(minfo,curPrice, newPrice,dir, balance, currency, false);

	//Strategy can disable to place order using size=0 and disable alert
	if (order.size == 0 && order.alert == IStrategy::Alert::disabled) return order;

	bool skipcycle = false;

	if (order.price <= 0) order.price = newPrice;
	if ((order.price - curPrice) * dir < 0) {
		if (calculateOrderFeeLessAdjust(order, balance, currency, dir, alerts, min_size)) skipcycle = true;;
	}
	double origOrderPrice = newPrice;


	if (!skipcycle) {


		do {
			prevSz = sz;

			newPrice = prevPrice * exp(step*dynmult*m);

			if ((newPrice - curPrice) * dir > 0) {
				newPrice = curPrice;
			}


			order= state.getNewOrder(minfo,curPrice, newPrice,dir, balance, currency, true);



			if (order.price <= 0) order.price = newPrice;
			if ((order.price - curPrice) * dir > 0) {
				order.price = curPrice;
			}

			sz = order.size;

			if (calculateOrderFeeLessAdjust(order, balance, currency, dir, alerts, min_size)) break;;

			cnt++;
			m = m*1.1;

		} while (cnt < 1000 && order.size == 0 && ((sz - prevSz)*dir>0  || cnt < 10));
	}
	auto lmsz = limitOrderMinMaxBalance(balance, order.size, order.price);
	if (lmsz.first) {
		order.size = lmsz.second;
		if (!order.size) order.alert = IStrategy::Alert::forced;
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



void MTrader::initialize() {
	std::string brokerImg;
	const IBrokerIcon *bicon = dynamic_cast<const IBrokerIcon*>(stock.get());
	if (bicon)
		brokerImg = bicon->getIconName();

	try {
		minfo = stock->getMarketInfo(cfg.pairsymb);

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
		else {
			statsvc->clear();
		}
		minfo.min_size = std::max(minfo.min_size, cfg.min_size);
	} catch (std::exception &e) {
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

void MTrader::loadState() {
	if (storage == nullptr) return;
	auto st = storage->load();
	need_load = false;


	if (!cfg.dry_run) {
		json::Value t = st["test_backup"];
		if (t.defined()) {
			st = t.replace("chart",st["chart"]);
		}
	}

	if (st.defined()) {


		auto state = st["state"];
		bool swapped = cfg.swap_symbols;
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
			lastTradeId = state["lastTradeId"];
			lastPriceOffset = state["lastPriceOffset"].getNumber();
			lastTradePrice = state["lastTradePrice"].getNumber();
			frozen_spread_side = state["frozen_side"].getInt();
			frozen_spread = state["frozen_spread"].getNumber();

			bool cfg_sliding = state["cfg_sliding_spread"].getBool();
			if (cfg_sliding != cfg.dynmult_sliding)
				lastPriceOffset = 0;
			achieve_mode = state["achieve_mode"].getBool();
			need_initial_reset = state["need_initial_reset"].getBool();
			swapped = state["swapped"].getBool();
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
		if (cfg.swap_symbols == swapped) {
			strategy.importState(st["strategy"], minfo);
		}

		if (cfg.dry_run) {
			test_backup = st["test_backup"];
			if (!test_backup.hasValue()) {
				test_backup = st.replace("chart",json::Value());
			}
		}

	}
	tempPr.broker = cfg.broker;
	tempPr.magic = magic;
	tempPr.uid = uid;
	tempPr.currency = minfo.currency_symbol;
	tempPr.asset = minfo.asset_symbol;
	tempPr.simulator = minfo.simulator;
	tempPr.invert_price = minfo.invert_price;
	if (strategy.isValid() && !trades.empty()) {
		wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(trades.back().eff_price));
	}
	if (!cfg.internal_balance && minfo.leverage == 0) {
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
		st.set("lastTradeId",lastTradeId);
		st.set("lastPriceOffset",lastPriceOffset);
		st.set("lastTradePrice", lastTradePrice);
		st.set("cfg_sliding_spread",cfg.dynmult_sliding);
		st.set("private_chart", minfo.private_chart||minfo.simulator);
		st.set("accumulated",accumulated);
		st.set("frozen_side", frozen_spread_side);
		st.set("frozen_spread", frozen_spread);
		if (achieve_mode) st.set("achieve_mode", achieve_mode);
		if (cfg.swap_symbols) st.set("swapped", cfg.swap_symbols);
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
	if (test_backup.hasValue()) {
		obj.set("test_backup", test_backup);
	}
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
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;
		last_price = trades.back().eff_price;
	}

	bool res = false;

	for (auto &&t : st.new_trades.trades) {
		if (t.eff_price <= 0 || t.price <= 0) throw std::runtime_error("Broker error - trade negative price");

		res = true;

		tempPr.tradeId = t.id.toString().str();
		tempPr.size = t.eff_size;
		tempPr.price = t.eff_price;
		tempPr.change = assetBal * (t.eff_price - last_price);
		tempPr.time = t.time;
		tempPr.acb_pnl = t.eff_size * enter_price_pos < 0
				?std::abs(t.eff_size > enter_price_pos)
						?enter_price_pos * ( t.eff_price - enter_price_sum/enter_price_pos)
						:-t.eff_size*(t.eff_price-enter_price_sum/enter_price_pos)
				:0;
		enter_price_pnl += tempPr.acb_pnl;
		if (last_price) statsvc->reportPerformance(tempPr);
		last_price = t.eff_price;
		if (minfo.leverage == 0) curBal -= t.price * t.size;
		spent_currency += t.price*t.size;

		if (t.eff_size) {
			double initState = assetBal - enter_price_pos;
			double size = t.eff_size;
			double newpos = size+enter_price_pos;
			if (newpos * enter_price_pos<= 0) {
				enter_price_sum = 0;
				enter_price_pos = 0;
				size = newpos;

				if (size * initState < 0) {
					auto df = sgn(initState)*std::min(std::abs(size), std::abs(initState));
					size += df;
				}
			}
			if (t.eff_size * enter_price_pos >= 0) {
				enter_price_sum += t.eff_price*size;
			} else {
				enter_price_sum += enter_price_sum * size/enter_price_pos;
			}
			enter_price_pos += size;
		}

		assetBal += t.eff_size;

		if (!achieve_mode && (cfg.enabled || first_cycle)) {
			auto norm = strategy.onTrade(minfo, t.eff_price, t.eff_size, assetBal, curBal);
			t.eff_size-=norm.normAccum;
			assetBal -= norm.normAccum;
			accumulated +=norm.normAccum;
			trades.push_back(TWBItem(t, last_np+=norm.normProfit, last_ap+=norm.normAccum, norm.neutralPrice));
		} else {
			trades.push_back(TWBItem(t, last_np, last_ap, 0, true));
		}
	}
	wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(last_price));

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
}

void MTrader::stop() {
	cfg.enabled = false;
}

void MTrader::reset(const ResetOptions &ropt) {
	init();
	dynmult.setMult(1, 1);
	stock->reset();
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
		wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(status.curPrice));
		if (!cfg.internal_balance && minfo.leverage == 0) {
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
	st.new_trades.trades.push_back(IStockApi::Trade {
		json::Value(json::String({"LOSS:", std::to_string(st.ticker.time)})),
		st.ticker.time,
		0,
		st.ticker.last,
		0,
		st.ticker.last,
	});
	processTrades(st);
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
				Status nest = st;
				nest.new_trades.trades.push_back(IStockApi::Trade {
					json::Value(json::String({"LOSS:", std::to_string(st.chartItem.time)})),
					st.chartItem.time,
					0,
					limitPrice,
					0,
					limitPrice,
				});
				processTrades(nest);
				strategy.reset();
			}
		}
	}
}

void MTrader::dropState() {
	storage->erase();
	statsvc->clear();
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

std::optional<double> MTrader::getInternalBalance() const {
	if (cfg.internal_balance) return position;
	else return std::optional<double>();
}
std::optional<double> MTrader::getPosition() const {
	return position;
}

std::optional<double> MTrader::getCurrency() const {
	return currency;
}

std::optional<double> MTrader::getInternalCurrencyBalance() const {
	if (cfg.internal_balance) return currency;
	else return std::optional<double>();
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
	return x.first;
}

std::pair<bool, double> MTrader::limitOrderMinMaxBalance(double balance, double orderSize, double price) const {
	const auto &min_balance = minfo.invert_price?cfg.max_balance:cfg.min_balance;
	const auto &max_balance = minfo.invert_price?cfg.min_balance:cfg.max_balance;
	double factor = minfo.invert_price?-1:1;

	if (orderSize < 0) {
		if (min_balance.has_value()) {
			double m = *min_balance * factor;
			if (balance < m) return {true,0};
			if (balance+orderSize < m) return {true,m - balance};
		}

	} else {
		if (max_balance.has_value()) {
			double m = *max_balance * factor;
			if (balance > m) return {true,0};
			if (balance+orderSize > m) return {true,m - balance};
		}

	}
	if (cfg.max_costs.has_value() && orderSize * balance >= 0) {
		double cost = orderSize * price;
		if (cost + spent_currency > cfg.max_costs) {
			return {true,0};
		}
	}
	return {false,orderSize};
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

bool MTrader::checkLeverage(const Order &order,double position, double currency, double &maxSize) const {
	double whole_pos = order.size + position;
	if (cfg.trade_within_budget && order.size * position > 0) {
		double alloc = strategy.calcCurrencyAllocation(order.price);
		if (alloc < 0) {
			maxSize = 0;
			return false;
		}
	}
	if (minfo.leverage && cfg.max_leverage) {
		if (order.size * position < 0)
			return true; //position reduce

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
			return false;
		}
	} else if (minfo.leverage == 0 && cfg.accept_loss == 0) {
		if (whole_pos < 0) {
			maxSize = -std::max(position,0.0);
			return false;
		}
		double vol = order.size * order.price;
		double min_cur = vol*0.01;
		if (currency - vol < min_cur) {
			vol = currency - min_cur;
			maxSize = std::max(vol/order.price,0.0);
			return false;
		}
	}
	return true;

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
	return enter_price_sum/enter_price_pos;
}
double MTrader::getEnterPricePos() const {
	return enter_price_pos;
}

double MTrader::getCosts() const {
	return spent_currency;
}
double MTrader::getRPnL() const {
	return enter_price_pnl;
}

void MTrader::updateEnterPrice() {
	double initState = (position_valid?position:0) - std::accumulate(trades.begin(), trades.end(), 0.0, [&](double a, const auto &tr) {
		return a + tr.eff_size;
	});
	double pos = initState;
	double eps = cfg.init_open?((minfo.invert_price?1.0/cfg.init_open:cfg.init_open)*initState):0;

	double pnl = 0;
	spent_currency = 0;
	for (const auto &tr : trades) {
		if (tr.eff_size) {
			double size = tr.eff_size;
			double newpos = size+pos;
			//new position is on other side or zero;
			if (newpos * pos <= 0) {

				pnl += pos * tr.eff_price - eps;
				//reset entry_price
				eps = 0;
				//reset pos
				pos = 0;
				//calculate remain size
				size = newpos;

			}
			if (tr.eff_size * pos >= 0) {
				eps += tr.eff_price*size;
			} else {
				pnl -= size * (tr.eff_price - eps/pos);
				eps += eps * size/pos;
			}
			pos += size;

		}
		spent_currency += tr.size * tr.price;
	}
	enter_price_sum = eps;
	enter_price_pos = pos;
	enter_price_pnl = pnl;
}
