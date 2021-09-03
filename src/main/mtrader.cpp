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
using ondra_shared::logInfo;
using ondra_shared::logNote;
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


	buy_mult = data["buy_mult"].getValueOrDefault(1.0);
	sell_mult = data["sell_mult"].getValueOrDefault(1.0);
	buy_step_mult = data["buy_step_mult"].getValueOrDefault(1.0);
	sell_step_mult = data["sell_step_mult"].getValueOrDefault(1.0);
	min_size = data["min_size"].getValueOrDefault(0.0);
	max_size = data["max_size"].getValueOrDefault(0.0);
	json::Value min_balance = data["min_balance"];
	json::Value max_balance = data["max_balance"];
	if (min_balance.type() == json::number) this->min_balance = min_balance.getNumber();
	if (max_balance.type() == json::number) this->max_balance = max_balance.getNumber();

	dynmult_raise = data["dynmult_raise"].getValueOrDefault(0.0);
	dynmult_fall = data["dynmult_fall"].getValueOrDefault(1.0);
	dynmult_cap = data["dynmult_cap"].getValueOrDefault(100.0);
	dynmult_mode = strDynmult_mode[data["dynmult_mode"].getValueOrDefault("half_alternate")];

	accept_loss = data["accept_loss"].getValueOrDefault(1);
	adj_timeout = std::max<unsigned int>(5,data["adj_timeout"].getUInt());
	max_leverage = data["max_leverage"].getValueOrDefault(0.0);

	force_spread = data["force_spread"].getValueOrDefault(0.0);
	report_order = data["report_order"].getValueOrDefault(0.0);
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
	zigzag = data["zigzag"].getValueOrDefault(false);
	swap_symbols= data["swap_symbols"].getValueOrDefault(false);
	emulate_leveraged=data["emulate_leveraged"].getValueOrDefault(0.0);
	reduce_on_leverage=data["reduce_on_leverage"].getBool();

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
	double diff = st.assetBalance - assetBalance;
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
			if (status.brokerAssetBalance.has_value()) wcfg.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.asset_symbol, *status.brokerAssetBalance);
			wcfg.walletDB.lock()->alloc({cfg.broker, minfo.wallet_id, minfo.asset_symbol, uid}, position);
		}

		std::string buy_order_error;
		std::string sell_order_error;

		currency_balance = status.currencyBalance;

		//update market fees
		minfo.fees = status.new_fees;
		//process all new trades
		bool anytrades = processTrades(status);
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

		double eq = strategy.getEquilibrium(status.assetBalance);
		bool delayed_trade_detect = false;

		if (!anytrades && cfg.enabled && std::abs(status.assetBalance - position) >= std::max(minfo.min_size,minfo.min_volume/status.curPrice))  {
			if (adj_wait>cfg.adj_timeout) {
				dorovnani(status, position, adj_wait_price);
				anytrades = processTrades(status);
				logNote("Adjust added: result - $1", position);
				adj_wait = 0;
			} else {
				if (adj_wait == 0) adj_wait_price = status.curPrice;
				adj_wait++;
				logNote("Need adjust $1 => $2, stage: $3/$4 price: $5",  position, status.assetBalance, adj_wait ,cfg.adj_timeout, adj_wait_price);
				delayed_trade_detect = true;
			}
		} else {
			adj_wait = 0;
		}


		bool need_alerts = trades.empty()?false:trades.back().size == 0;

		double lastTradeSize = trades.empty()?0:trades.back().eff_size;
		if (lastTradePrice == 0 ) {
			lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium(status.assetBalance):status.curPrice;
			if (!std::isfinite(lastTradePrice)) lastTradePrice = status.curPrice;		}

		if (cfg.dynmult_sliding) {

			if (lastPriceOffset != 0) {
				lastTradePrice = lastPriceOffset+status.spreadCenter;
			}

			need_alerts = true;
			double prevLTP = lastTradePrice;
			double low = lastTradePrice * std::exp(-status.curStep*cfg.buy_step_mult);
			double high = lastTradePrice * std::exp(status.curStep*cfg.sell_step_mult);
			double eq = strategy.getEquilibrium(status.assetBalance);
			if (high < eq) {
				lastTradePrice = eq/std::exp(status.curStep*cfg.sell_step_mult);
				logDebug("Sliding - high < eq - $1 < $2, old_center = $4, new center = $3", high, eq, lastTradePrice, prevLTP);
			} else if (low > eq) {
				lastTradePrice = eq/std::exp(-status.curStep*cfg.buy_step_mult);
				logDebug("Sliding - low > eq - $1 > $2, old_center = $4, new center = $3", low, eq, lastTradePrice, prevLTP);
			}

		} else if (!need_alerts
					&& std::isfinite(eq)     //equilibrium is finite
					&& eq > 0				 //equilibrium is not zero or negatiove
					&& (lastTradePrice * std::exp(-status.curStep)>eq //eq is not in reach to lastTradePrice
						|| lastTradePrice * std::exp(status.curStep)<eq)) { //eq is not in reach to lastTradePrice
			if (cfg.buy_mult < 0.99 || cfg.sell_mult > 1.01 || cfg.max_size != 0) {
				logDebug("Enforced alerts because configuration");
				need_alerts = true;
			} else {
				lastTradePrice = strategy.getCenterPrice(lastTradePrice, status.assetBalance);
			}
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
				if (!trades.empty()) {
					lp = trades.back().eff_price;
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
					if (cfg.zigzag) updateZigzagLevels();
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



							//calculate buy order
						buyorder = calculateOrder(grant_trade?status.ticker.bid*1.5:lastTradePrice,
														grant_trade?-0.1:-status.curStep*cfg.buy_step_mult,
														dynmult.getBuyMult(),
														status.ticker.bid,
														position,
														status.currencyBalance,
														cfg.buy_mult,
														zigzaglevels,
														grant_trade?false:need_alerts);
							//calculate sell order
						sellorder = calculateOrder(grant_trade?status.ticker.ask*0.85:lastTradePrice,
														 grant_trade?0.1:status.curStep*cfg.sell_step_mult,
														 dynmult.getSellMult(),
														 status.ticker.ask,
														 position,
														 status.currencyBalance,
														 cfg.sell_mult,
														 zigzaglevels,
														 grant_trade?false:need_alerts);

					}
					if (grant_trade) {
						sellorder.alert = IStrategy::Alert::disabled;
						buyorder.alert = IStrategy::Alert::disabled;
						if (status.curPrice < eq) {
							sellorder.size = 0;
						} else if (status.curPrice > eq) {
							buyorder.size = 0;
						}
					}

					try {
						setOrder(orders.buy, buyorder, buy_alert);
						if (!orders.buy.has_value()) {
							acceptLoss(status, 1);
						}
					} catch (std::exception &e) {
						buy_order_error = e.what();
						acceptLoss(status, 1);
					}
					try {
						setOrder(orders.sell, sellorder, sell_alert);
						if (!orders.sell.has_value()) {
							acceptLoss(status, -1);
						}
					} catch (std::exception &e) {
						sell_order_error = e.what();
						acceptLoss(status,-1);
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

		if (!cfg.hidden) {
			int last_trade_dir = !anytrades?0:sgn(lastTradeSize);
			if (fast_trade) {
				if (last_trade_dir < 0) orders.sell.reset();
				if (last_trade_dir > 0) orders.buy.reset();
			}
			//report orders to UI
			statsvc->reportOrders(orders.buy,orders.sell);
			//report trades to UI
			statsvc->reportTrades(position, trades);
			//report price to UI
			statsvc->reportPrice(status.curPrice);
			//report misc
			auto minmax = strategy.calcSafeRange(minfo,status.assetAvailBalance,status.currencyAvailBalance);
			auto budget = strategy.getBudgetInfo();
			auto equil = strategy.getEquilibrium(status.assetBalance);
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
				equil,
				status.curPrice * (exp(status.curStep) - 1),
				dynmult.getBuyMult(),
				dynmult.getSellMult(),
				minmax.min,
				minmax.max,
				budget.total,
				budget.assets,
				budget_extra,
				trades.size(),
				trades.empty()?0:(trades.back().time-trades[0].time),
				lastTradePrice
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



		//save state
		saveState();
		first_cycle = false;

	} catch (std::exception &e) {
		statsvc->reportTrades(position,trades);
		std::string error;
		error.append(e.what());
		statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
		statsvc->reportMisc(IStatSvc::MiscData{
			0,false,0,0,dynmult.getBuyMult(),dynmult.getSellMult(),0,0,0,0,0,
			trades.size(),trades.empty()?0UL:(trades.back().time-trades[0].time),lastTradePrice
		});
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
		} catch (std::exception &e) {
			ondra_shared::logError("$1", e.what());
		}
	}
	return ret;
}


void MTrader::setOrder(std::optional<IStockApi::Order> &orig, Order neworder, std::optional<double> &alert) {
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
		if (neworder.alert == IStrategy::Alert::forced) {
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

		IStockApi::Order n {json::undefined, magic, neworder.size, neworder.price};
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
	if (cfg.internal_balance && currency_balance.has_value() && position_valid) {
		auto sumt = std::accumulate(res.new_trades.trades.begin(),
				res.new_trades.trades.end(),std::pair<double,double>(0,0),sumTrades<IStockApi::Trade>);
		res.assetBalance = sumt.first;
		res.currencyBalance = sumt.second;
		if (minfo.leverage) res.currencyBalance = 0;
		res.assetBalance += position;
		res.currencyBalance += *currency_balance;
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
		res.currencyAvailBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),*res.brokerAssetBalance);
	}

	res.new_fees = stock->getFees(cfg.pairsymb);

	auto ticker = stock->getTicker(cfg.pairsymb);
	if (buy_alert.has_value() && *buy_alert > ticker.bid) ticker.bid = *buy_alert;
	if (sell_alert.has_value() && *sell_alert < ticker.ask) ticker.ask = *sell_alert;
	res.ticker = ticker;
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);


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

bool MTrader::calculateOrderFeeLessAdjust(Order &order, int dir, double mult, bool alerts, double min_size, const ZigZagLevels &zlev) const {

	bool enabledAlert = order.alert != IStrategy::Alert::disabled;

	Strategy::adjustOrder(dir, mult, alerts, order);

	modifyOrder(zlev,dir, order);



	if (order.alert == IStrategy::Alert::forced) {
		return true;
	}

	double d;
	if (!checkLeverage(order, d))  {
		order.size = d;
		if (d == 0) {
			if (enabledAlert) order.alert = IStrategy::Alert::forced;
			return true;
		}
	}

	if (cfg.max_size && std::fabs(order.size) > cfg.max_size) {
		order.size = cfg.max_size*dir;
	}
	if (std::fabs(order.size) < min_size) order.size = 0;
	if (minfo.min_volume) {
		double op = order.price;
		double os = order.size;
		minfo.addFees(os,op);
		double vol = std::fabs(op * os);
		if (vol < minfo.min_volume) order.size = 0;
	}

	return false;

}

MTrader::Order MTrader::calculateOrderFeeLess(
		double prevPrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
		double currency,
		double mult,
		const ZigZagLevels &zlev,
		bool alerts) const {

	double m = 1;

	int cnt = 0;
	double prevSz = 0;
	double sz = 0;
	Order order;

	double min_size = std::max(cfg.min_size, minfo.min_size);
	double dir = sgn(-step);

	double newPrice = prevPrice *exp(step*dynmult*m);
	if ((newPrice - curPrice) * dir > 0) {
		newPrice = curPrice;
		prevPrice = newPrice /exp(step*dynmult*m);
	}
	order= strategy.getNewOrder(minfo,curPrice, newPrice,dir, balance, currency, false);

	//Strategy can disable to place order using size=0 and disable alert
	if (order.size == 0 && order.alert == IStrategy::Alert::disabled) return order;

	bool skipcycle = false;

	if (order.price <= 0) order.price = newPrice;
	if ((order.price - curPrice) * dir < 0) {
		if (calculateOrderFeeLessAdjust(order, dir, mult, alerts, min_size, zlev)) return order;
		if (order.size != 0)
			skipcycle = true;
	}
	double origOrderPrice = order.price;


	if (!skipcycle) {


		do {
			prevSz = sz;

			newPrice = prevPrice * exp(step*dynmult*m);

			if ((newPrice - curPrice) * dir > 0) {
				newPrice = curPrice;
			}

			order= strategy.getNewOrder(minfo,curPrice, newPrice,dir, balance, currency, true);



			if (order.price <= 0) order.price = newPrice;
			if ((order.price - curPrice) * dir > 0) {
				order.price = curPrice;
			}

			sz = order.size;

			if (calculateOrderFeeLessAdjust(order, dir, mult, alerts, min_size, zlev)) return order;

			cnt++;
			m = m*1.1;

		} while (cnt < 1000 && order.size == 0 && ((sz - prevSz)*dir>0  || cnt < 10));
	}
	auto lmsz = limitOrderMinMaxBalance(balance, order.size);
	if (lmsz.first) {
		order.size = lmsz.second;
		order.alert = !order.size?IStrategy::Alert::forced:IStrategy::Alert::enabled;
	}
	if (order.size == 0 && order.alert != IStrategy::Alert::disabled) {
		order.price = origOrderPrice;
		order.alert = IStrategy::Alert::forced;
	}

	return order;

}

MTrader::Order MTrader::calculateOrder(
		double lastTradePrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
		double currency,
		double mult,
		const ZigZagLevels &zlev,
		bool alerts) const {

		double fakeSize = -step;
		//remove fees from curPrice to effectively put order inside of bid/ask spread
		minfo.removeFees(fakeSize, curPrice);
		//calculate order
		Order order(calculateOrderFeeLess(lastTradePrice, step,dynmult,curPrice,balance,currency,mult,zlev,alerts));
		//apply fees
		if (order.alert != IStrategy::Alert::forced && order.size) minfo.addFees(order.size, order.price);

		return order;

}

void MTrader::updateZigzagLevels() {
	zigzaglevels.levels.clear();
	zigzaglevels.direction = 0;
	if (trades.size()<4) return;
	double sumTrades = std::accumulate(trades.begin(), trades.end(), 0.0, [](double x, const auto &z) {
		return x + std::abs(z.eff_size);
	});
	double limit = sumTrades/trades.size();

	logDebug("(Zigzag) Limit set $1", limit*2);

	auto iter = trades.rbegin();
	auto end = trades.rend();
	while (iter != end && iter->size == 0) ++iter;
	if (iter != end)  {
		double sz = iter->eff_size;
		if (std::abs(sz)>=limit*2) sz = 0;
		zigzaglevels.levels.push_back({
			sz,
			iter->eff_price,
		});
		{
			const auto &bb = zigzaglevels.levels.back();
			logDebug("(Zigzag) ZigZagLevels 1th level update: amount=$1, price=$2", bb.amount, bb.price);
		}
		zigzaglevels.direction = sgn(iter->size);
		++iter;
		int level = 2;
		while (iter != end && iter->size * zigzaglevels.direction >= 0) {
			level++;
			const auto &b = zigzaglevels.levels.back();
			double sz = iter->eff_size+b.amount;
			if (std::abs(sz) < limit*level) {
				zigzaglevels.levels.push_back({
					sz, (b.amount * b.price + iter->eff_size*iter->eff_price)/(b.amount + iter->eff_size)
				});
				const auto &bb = zigzaglevels.levels.back();
				logDebug("(Zigzag) ZigZagLevels 2nd level update: amount=$1, price=$2", bb.amount, bb.price);
			}
			++iter;
		}
	}
}

void MTrader::modifyOrder(const ZigZagLevels &zlevs, double ,  Order &order) const {
	if ((order.size * zlevs.direction < 0) && (minfo.leverage == 0 || order.size * position <0)) {
		for (const auto &l : zlevs.levels) {
			if ((l.price - order.price)* zlevs.direction < 0 && (std::abs(order.size) < std::abs(l.amount) )) {
				logDebug("(Zigzag) Zigzag active: order_price/level_price=($1 => $3), order_size/new_size=($2 => $4)",
						order.price, order.size, l.price, -l.amount	);
				order.size = -l.amount;
			}
		}
	}
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
			if (state["currency_balance"].hasValue()) currency_balance = state["currency_balance"].getNumber();
			json::Value accval = state["account_value"];
			recalc = state["recalc"].getBool();
			std::size_t nuid = state["uid"].getUInt();
			if (nuid) uid = nuid;
			lastTradeId = state["lastTradeId"];
			lastPriceOffset = state["lastPriceOffset"].getNumber();
			lastTradePrice = state["lastTradePrice"].getNumber();
			bool cfg_sliding = state["cfg_sliding_spread"].getBool();
			if (cfg_sliding != cfg.dynmult_sliding)
				lastPriceOffset = 0;
			achieve_mode = state["achieve_mode"].getBool();
			need_initial_reset = state["need_initial_reset"].getBool();
			swapped = state["swapped"].getBool();
			adj_wait = state["adj_wait"].getUInt();
			adj_wait_price = state["adj_wait_price"].getNumber();
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
	if (cfg.zigzag) updateZigzagLevels();
	if (strategy.isValid() && !trades.empty()) {
		wcfg.walletDB.lock()->alloc(getWalletBalanceKey(), strategy.calcCurrencyAllocation(trades.back().eff_price));
	}
	if (!cfg.internal_balance && minfo.leverage == 0 && position_valid) {
		wcfg.walletDB.lock()->alloc(getWalletAssetKey(), position);
	}

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
		if (currency_balance.has_value())
			st.set("currency_balance", *currency_balance);
		st.set("recalc",recalc);
		st.set("uid",uid);
		st.set("lastTradeId",lastTradeId);
		st.set("lastPriceOffset",lastPriceOffset);
		st.set("lastTradePrice", lastTradePrice);
		st.set("cfg_sliding_spread",cfg.dynmult_sliding);
		st.set("private_chart", minfo.private_chart||minfo.simulator);
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
		if (last_price) statsvc->reportPerformance(tempPr);
		last_price = t.eff_price;
		assetBal += t.eff_size;
		if (minfo.leverage == 0) curBal -= t.price * t.size;

		if (!achieve_mode && (cfg.enabled || first_cycle)) {
			auto norm = strategy.onTrade(minfo, t.eff_price, t.eff_size, assetBal, curBal);
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
		position = assets = wcfg.walletDB.lock_shared()->adjBalance(getWalletAssetKey(), status.assetUnadjustedBalance);
	}
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
			auto order = calculateOrder(lastTradePrice, -st.curStep * 2 * dir, 1, lastTradePrice, st.assetBalance, st.currencyBalance, 1.0, zigzaglevels,true);
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

std::optional<double> MTrader::getInternalCurrencyBalance() const {
	if (cfg.internal_balance) return currency_balance;
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

MTrader::VisRes MTrader::visualizeSpread(
		std::function<std::optional<ChartItem>()> &&source,
		double sma,
		double stdev,
		double force_spread,
		double mult,
		double dyn_raise,
		double dyn_fall,
		double dyn_cap,
		json::StrViewA dynMode,
		bool sliding,
		bool dyn_mult,
		bool strip,
		bool onlyTrades) {
	DynMultControl dynmult({dyn_raise, dyn_fall, dyn_cap, strDynmult_mode[dynMode], dyn_mult});
	VisRes res;
	double last = 0;
	double last_price = 0;
	std::vector<double> prices;
	std::size_t isma = sma*60;
	std::size_t istdev = stdev * 60;
	if (force_spread>0) istdev = 5;
	std::size_t mx = std::max(isma+istdev, 2*istdev);
	for (auto k = source(); k.has_value(); k = source()) {
		double p = k->last;
		if (last || sliding) {
	/*		if (minfo.invert_price) p = 1.0/p;*/
			prices.push_back(p);
			auto spread_info = stCalcSpread(prices.end()-std::min(mx,prices.size()), prices.end(), isma, istdev);
			if (force_spread>0) spread_info.spread = force_spread;
			double spread = spread_info.spread;
			double center = sliding?spread_info.center:0;
			double low = (center+last) * std::exp(-spread*mult*dynmult.getBuyMult());
			double high = (center+last) * std::exp(spread*mult*dynmult.getSellMult());
			if (sliding && last_price) {
				double low_max = last_price*std::exp(-spread*0.01);
				double high_min = last_price*std::exp(spread*0.01);
				if (low > low_max) {
					high = low_max + (high-low);
					low = low_max;
				}
				if (high < high_min) {
					low = high_min - (high-low);
					high = high_min;

				}
				low = std::min(low_max, low);
				high = std::max(high_min, high);
			}
			double size = 0;
			if (p > high) {
				last_price = high;
				last = high-center; size = -1;dynmult.update(false,true);
			}
			else if (p < low) {
				last_price = low;
				last = low-center; size = 1;dynmult.update(true,false);
			}
			else {
				dynmult.update(false,false);
			}
			if (size || !onlyTrades) res.chart.push_back(VisRes::Item{
				p, low, high, size,k->time
			});
		} else {
			last = p;
		}
	}
	if (strip && res.chart.size()>10) res.chart.erase(res.chart.begin(), res.chart.begin()+res.chart.size()/2);
	return res;
}


bool MTrader::checkMinMaxBalance(double balance, double orderSize) const {
	auto x = limitOrderMinMaxBalance(balance, orderSize);
	return x.first;
}

std::pair<bool, double> MTrader::limitOrderMinMaxBalance(double balance, double orderSize) const {
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
	return {false,orderSize};
}

void MTrader::setInternalBalancies(double assets, double currency) {
	position = assets;
	currency_balance = currency;
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

bool MTrader::checkLeverage(const Order &order, double &maxSize) const {
	double whole_pos = order.size + position;
	if (minfo.leverage && cfg.max_leverage && currency_balance.has_value() ) {
		if (std::abs(whole_pos) < std::abs(position) && (whole_pos * position) > 0)
			return true; //position reduce

		double bal = *currency_balance;

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
		if (*currency_balance - vol < min_cur) {
			vol = *currency_balance - min_cur;
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
	double mp = (*currency_balance/st.curPrice) * cfg.max_leverage;
	if (mp < std::abs(st.assetBalance)) {
		maxPosition = sgn(st.assetBalance) * mp;
		return true;
	} else {
		return false;
	}
}


