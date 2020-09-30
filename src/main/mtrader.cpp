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
	dynmult_mode = strDynmult_mode[data["dynmult_mode"].getValueOrDefault("half_alternate")];

	accept_loss = data["accept_loss"].getValueOrDefault(1);
	max_leverage = data["max_leverage"].getValueOrDefault(0);
	external_balance= data["ext_bal"].getValueOrDefault(0.0);

	force_spread = data["force_spread"].getValueOrDefault(0.0);
	report_position_offset = data["report_position_offset"].getValueOrDefault(0.0);
	report_order = data["report_order"].getValueOrDefault(0.0);
	grant_trade_minutes = static_cast<unsigned int>(data["grant_trade_hours"].getValueOrDefault(0.0)*60);

	spread_calc_sma_hours = static_cast<unsigned int>(data["spread_calc_sma_hours"].getValueOrDefault(24.0)*60.0);
	spread_calc_stdev_hours = static_cast<unsigned int>(data["spread_calc_stdev_hours"].getValueOrDefault(4.0)*60.0);

	dry_run = force_dry_run || data["dry_run"].getValueOrDefault(false);
	internal_balance = data["internal_balance"].getValueOrDefault(false) || dry_run;
	detect_manual_trades= data["detect_manual_trades"].getValueOrDefault(false);
	enabled= data["enabled"].getValueOrDefault(true);
	hidden = data["hidden"].getValueOrDefault(false);
	alerts= data["alerts"].getValueOrDefault(true);
	delayed_alerts= data["delayed_alerts"].getValueOrDefault(true);
	dynmult_scale = data["dynmult_scale"].getValueOrDefault(true);
	dynmult_sliding = data["dynmult_sliding"].getValueOrDefault(false);
	dynmult_mult = data["dynmult_mult"].getValueOrDefault(false);
	zigzag = data["zigzag"].getValueOrDefault(false);
	swap_symbols= data["swap_symbols"].getValueOrDefault(false);


	if (dynmult_raise > 1e6) throw std::runtime_error("'dynmult_raise' is too big");
	if (dynmult_raise < 0) throw std::runtime_error("'dynmult_raise' is too small");
	if (dynmult_fall > 100) throw std::runtime_error("'dynmult_fall' must be below 100");
	if (dynmult_fall <= 0) throw std::runtime_error("'dynmult_fall' must not be negative or zero");

}

MTrader::MTrader(IStockSelector &stock_selector,
		StoragePtr &&storage,
		PStatSvc &&statsvc,
		PWalletDB walletDB,
		Config config)
:stock(selectStock(stock_selector,config))
,cfg(config)
,storage(std::move(storage))
,statsvc(std::move(statsvc))
,walletDB(walletDB)
,strategy(config.strategy)
,dynmult(cfg.dynmult_raise,cfg.dynmult_fall, cfg.dynmult_mode, cfg.dynmult_mult)
{
	//probe that broker is valid configured
	stock->testBroker();
	magic = this->statsvc->getHash() & 0xFFFFFFFF;
	std::random_device rnd;
	uid = 0;
	while (!uid) {
		uid = rnd();
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
	st.enable_alerts = false;
}

void MTrader::perform(bool manually) {

	try {
		init();


		//Get opened orders
		auto orders = getOrders();
		//get current status
		auto status = getMarketStatus();

		std::string buy_order_error;
		std::string sell_order_error;

		internal_balance = status.assetBalance;
		currency_balance = status.currencyBalance;


		//update market fees
		minfo.fees = status.new_fees;
		//process all new trades
		bool anytrades = processTrades(status);
		bool fast_trade = false;
		if (anytrades && ((!orders.buy.has_value() && !buy_alert.has_value())
				|| (!orders.sell.has_value() && !sell_alert.has_value()))) {
			recalc = true;
			fast_trade = true;
			sell_alert.reset();
			buy_alert.reset();
		}

		double lastTradeSize = trades.empty()?0:trades.back().eff_size;
		double lastTradePrice;
		if (lastPriceOffset == 0) {
			lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium(status.assetBalance):status.curPrice;
			if (!std::isfinite(lastTradePrice)) lastTradePrice = status.curPrice;
		} else {
			lastTradePrice = lastPriceOffset+status.spreadCenter;
		}
		if (cfg.dynmult_sliding) {
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
			} else  if (!cfg.alerts && !cfg.delayed_alerts && !grant_trade
					&& (trades.empty() || trades.back().size != 0)) {
				grant_trade = !checkEquilibriumClose(status,lastTradePrice);
			}

			if (recalc) {
				update_dynmult(lastTradeSize > 0, lastTradeSize < 0);
				if (cfg.zigzag) updateZigzagLevels();
			}

			if (grant_trade) {
				dynmult.reset();
			}


			//process alerts
			if (sell_alert.has_value() && status.ticker.last >= *sell_alert) {
				alertTrigger(status, *sell_alert);
				update_dynmult(false,true);
			}
			if (buy_alert.has_value() && status.ticker.last <= *buy_alert) {
				alertTrigger(status, *buy_alert);
				update_dynmult(true,false);
			}
			if (!status.new_trades.trades.empty()) {
				processTrades(status);
			}


			strategy.onIdle(minfo, status.ticker, status.assetBalance, status.currencyBalance);

			if (status.curStep) {

				if (!cfg.enabled)  {
					if (orders.buy.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic,orders.buy->id,0);
					if (orders.sell.has_value())
						stock->placeOrder(cfg.pairsymb,0,0,magic,orders.sell->id,0);
					if (!cfg.hidden) statsvc->reportError(IStatSvc::ErrorObj("Automatic trading is disabled"));
				} else {



						//calculate buy order
					Order buyorder = calculateOrder(grant_trade?status.ticker.bid*1.5:lastTradePrice,
													grant_trade?-0.1:-status.curStep*cfg.buy_step_mult,
													dynmult.getBuyMult(),
													status.ticker.bid,
													status.assetBalance,
													status.currencyBalance,
													cfg.buy_mult,
													zigzaglevels,
													grant_trade?false:status.enable_alerts);
						//calculate sell order
					Order sellorder = calculateOrder(grant_trade?status.ticker.ask*0.85:lastTradePrice,
													 grant_trade?0.1:status.curStep*cfg.sell_step_mult,
													 dynmult.getSellMult(),
													 status.ticker.ask,
													 status.assetBalance,
													 status.currencyBalance,
													 cfg.sell_mult,
													 zigzaglevels,
													 grant_trade?false:status.enable_alerts);


					if (grant_trade) {
						sellorder.alert = IStrategy::Alert::disabled;
						buyorder.alert = IStrategy::Alert::disabled;
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
			int last_trade_dir = !anytrades?0:sgn(status.new_trades.trades.back().size);
			if (fast_trade) {
				if (last_trade_dir < 0) orders.sell.reset();
				if (last_trade_dir > 0) orders.buy.reset();
			}
			//report orders to UI
			statsvc->reportOrders(orders.buy,orders.sell);
			//report trades to UI
			statsvc->reportTrades(trades);
			//report price to UI
			statsvc->reportPrice(status.curPrice);
			//report misc
			auto minmax = strategy.calcSafeRange(minfo, status.assetBalance, status.currencyBalance);
			auto budget = strategy.getBudgetInfo();
			std::optional<double> budget_extra;
			if (minfo.leverage == 0)
				budget_extra =  status.currencyBalance - walletDB.lock_shared()->query(WalletDB::KeyQuery(
												cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid)).total()+cfg.external_balance;

			statsvc->reportMisc(IStatSvc::MiscData{
				last_trade_dir,
				strategy.getEquilibrium(status.assetBalance),
				status.curPrice * (exp(status.curStep) - 1),
				dynmult.getBuyMult(),
				dynmult.getSellMult(),
				minmax.min,
				minmax.max,
				budget.total,
				budget.assets,
				budget_extra,
				trades.size(),
				trades.empty()?0:(trades.back().time-trades[0].time)
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
		statsvc->reportTrades(trades);
		std::string error;
		error.append(e.what());
		statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
		statsvc->reportMisc(IStatSvc::MiscData{
			0,0,0,dynmult.getBuyMult(),dynmult.getSellMult(),0,0,0,0,
			trades.size(),trades.empty()?0:(trades.back().time-trades[0].time)
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
		if (neworder.alert == IStrategy::Alert::forced) {
			if (orig.has_value() && orig->id.hasValue()) {
				//cancel current order
				stock->placeOrder(cfg.pairsymb,0,0,nullptr,orig->id,0);
			}
			alert = neworder.price;
			neworder.update(orig);
			return;
		}
		if (neworder.size == 0 && orig.has_value()) {
			return;
		}
		IStockApi::Order n {json::undefined, magic, neworder.size, neworder.price};
		try {
			checkLeverage(neworder);
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
				orig.reset();
			} else if (placeid != replaceid) {
				n.id = placeid;
				orig = n;
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

	if (!res.new_trades.trades.empty() || !internal_balance.has_value() || !currency_balance.has_value())
	{
		if (cfg.internal_balance && internal_balance.has_value() && currency_balance.has_value()) {
			auto sumt = std::accumulate(res.new_trades.trades.begin(),
					res.new_trades.trades.end(),std::pair<double,double>(0,0),sumTrades<IStockApi::Trade>);
			res.assetBalance = sumt.first;
			res.currencyBalance = minfo.leverage?0:sumt.second;
			if (internal_balance.has_value()) res.assetBalance += *internal_balance;
			if (currency_balance.has_value()) res.currencyBalance += *currency_balance;
		} else {
			res.assetBalance = stock->getBalance(minfo.asset_symbol, cfg.pairsymb);
			res.currencyBalance = stock->getBalance(minfo.currency_symbol, cfg.pairsymb);
			auto wdb = walletDB.lock_shared();
			res.assetBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid), res.assetBalance);
			res.currencyBalance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid), res.currencyBalance);
		}
	} else {
		res.currencyBalance = *currency_balance;
		res.assetBalance = *internal_balance;
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

	if (cfg.delayed_alerts && !trades.empty()) {
		if (res.new_trades.trades.empty()) {
			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto ellapsed = now - trades.back().time;
			using TT = decltype(ellapsed);
			TT period;
			if (cfg.accept_loss) {
				period = cfg.accept_loss * static_cast<TT>(3600*1000);
			} else {
				if (period_cache) {
					period = period_cache;
				} else {
					auto cnt = std::count_if(trades.begin(), trades.end(), [&](const IStatSvc::TradeRecord &t) {
						return t.size != 0;
					});
					auto sum = std::accumulate(trades.begin()+1, trades.end(), std::pair(trades[0].time, TT(0)), [&](const auto &c, const auto &t) {
						if (t.size != 0) return std::pair(t.time, c.second+(t.time-c.first)); else return c;
					});
					if (cnt>1) period = sum.second /(cnt-1); else period = 0;
					period_cache = period;
				}
			}
			res.enable_alerts = ellapsed > period;
			res.enable_alerts_after_minutes = period - ellapsed;
		} else {
			res.enable_alerts = false;
			period_cache = 0;
		}

	} else {
		res.enable_alerts = cfg.alerts;
	}

	auto step = calcSpread();
	res.curStep = cfg.force_spread>0?cfg.force_spread:step.spread;
	if (cfg.dynmult_sliding) {
		res.spreadCenter = step.center;
	} else {
		res.spreadCenter = 0;
	}


	return res;
}

bool MTrader::calculateOrderFeeLessAdjust(Order &order, int dir, double mult, bool alerts, double min_size, const ZigZagLevels &zlev) const {

	Strategy::adjustOrder(dir, mult, alerts, order);

	modifyOrder(zlev,dir, order);



	if (order.alert == IStrategy::Alert::forced) {
		return true;
	}

	double d;
	if (!checkLeverage(order, d))  {
		order.size = d;
		if (d == 0) {
			order.alert = IStrategy::Alert::forced;
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

	double newPrice = prevPrice * exp(step*dynmult*m);
	double newPriceNoScale= prevPrice * exp(step*m);

	order= strategy.getNewOrder(minfo,curPrice, cfg.dynmult_scale?newPrice:newPriceNoScale,dir, balance, currency, false);

	if (order.price <= 0) order.price = newPrice;
	if ((order.price - curPrice) * dir < 0) {
		if (calculateOrderFeeLessAdjust(order, dir, mult, alerts, min_size, zlev)) return order;
		if (order.size != 0) return order;
	}



	do {
		prevSz = sz;

		newPrice = prevPrice * exp(step*dynmult*m);
		newPriceNoScale= prevPrice * exp(step*m);

		if ((newPrice - curPrice) * dir > 0) {
			newPrice = curPrice;
		}
		if ((newPriceNoScale - curPrice) * dir > 0) {
			newPriceNoScale = curPrice;
		}

		order= strategy.getNewOrder(minfo,curPrice, cfg.dynmult_scale?newPrice:newPriceNoScale,dir, balance, currency, true);



		if (order.price <= 0) order.price = newPrice;
		if ((order.price - curPrice) * dir > 0) {
			order.price = curPrice;
		}

		sz = order.size;

		if (calculateOrderFeeLessAdjust(order, dir, mult, alerts, min_size, zlev)) return order;

		cnt++;
		m = m*1.1;

	} while (cnt < 1000 && order.size == 0 && ((sz - prevSz)*dir>0  || cnt < 10));
	auto lmsz = limitOrderMinMaxBalance(balance, order.size);
	if (lmsz.first) {
		order.size = lmsz.second;
		order.alert = IStrategy::Alert::forced;
	} else {
		order.alert = !order.size && alerts?IStrategy::Alert::forced:IStrategy::Alert::disabled;
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
		if (order.alert != IStrategy::Alert::forced) minfo.addFees(order.size, order.price);

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
	if (sgn(order.size) * zlevs.direction < 0) {
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
						cfg.report_position_offset, cfg.report_order,
						minfo.invert_price, minfo.leverage != 0, minfo.simulator });
		}
		else {
			statsvc->clear();
		}
	} catch (std::exception &e) {
		this->statsvc->setInfo(
				IStatSvc::Info {cfg.title, "???",
								"???",
								"???",
								brokerImg,
								cfg.report_position_offset,
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
			if (cfg.internal_balance) {
				if (state["internal_balance"].hasValue()) internal_balance = state["internal_balance"].getNumber();
				if (state["currency_balance"].hasValue()) currency_balance = state["currency_balance"].getNumber();
			}
			json::Value accval = state["account_value"];
			recalc = state["recalc"].getBool();
			std::size_t nuid = state["uid"].getUInt();
			if (nuid) uid = nuid;
			lastTradeId = state["lastTradeId"];
			lastPriceOffset = state["lastPriceOffset"].getNumber();
			bool cfg_sliding = state["cfg_sliding_spread"].getBool();
			if (cfg_sliding != cfg.dynmult_sliding)
				lastPriceOffset = 0;
			achieve_mode = state["achieve_mode"].getBool();
			swapped = state["swapped"].getBool();
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
		walletDB.lock()->alloc(getWalletKey(), strategy.calcCurrencyAllocation(trades.back().eff_price));
	}

}

void MTrader::saveState() {
	if (storage == nullptr || need_load) return;
	json::Object obj;

	{
		auto st = obj.object("state");
		st.set("buy_dynmult", dynmult.getBuyMult());
		st.set("sell_dynmult", dynmult.getSellMult());
		if (internal_balance.has_value())
			st.set("internal_balance", *internal_balance);
		if (currency_balance.has_value())
			st.set("currency_balance", *currency_balance);
		st.set("recalc",recalc);
		st.set("uid",uid);
		st.set("lastTradeId",lastTradeId);
		st.set("lastPriceOffset",lastPriceOffset);
		st.set("cfg_sliding_spread",cfg.dynmult_sliding);
		st.set("private_chart", minfo.private_chart||minfo.simulator);
		st.set("achieve_mode", achieve_mode);
		st.set("swapped", cfg.swap_symbols);
	}
	{
		auto ch = obj.array("chart");
		for (auto &&itm: chart) {
			ch.push_back(json::Object("time", itm.time)
				  ("ask",minfo.invert_price?1.0/itm.ask:itm.ask)
				  ("bid",minfo.invert_price?1.0/itm.bid:itm.bid)
				  ("last",minfo.invert_price?1.0/itm.last:itm.last));
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


	StringView<IStockApi::Trade> new_trades(st.new_trades.trades);

	//Remove duplicate trades
	//which can happen by failed synchronization
	//while the new trade is already in current trades
	while (!new_trades.empty() && !trades.empty()
			&& std::find_if(trades.begin(), trades.end(),[&](const IStockApi::Trade &t) {
				return t.id == new_trades[0].id;
			}) != trades.end()) {
			new_trades = new_trades.substr(1);
	}

	if (new_trades.empty()) return false;



	auto z = std::accumulate(new_trades.begin(), new_trades.end(),std::pair<double,double>(st.assetBalance,st.currencyBalance),
			[](const std::pair<double,double> &x, const IStockApi::Trade &y) {
		return std::pair<double,double>(x.first - y.eff_size, x.second + y.eff_size*y.eff_price);}
	);


	double last_np = 0;
	double last_ap = 0;
	double last_price = 0;
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;
		last_price = trades.back().eff_price;
	}



	for (auto &&t : new_trades) {


		if (t.eff_price <= 0 || t.price <= 0) throw std::runtime_error("Broker error - trade negative price");

		tempPr.tradeId = t.id.toString().str();
		tempPr.size = t.eff_size;
		tempPr.price = t.eff_price;
		tempPr.change = z.first * (t.eff_price - last_price);
		tempPr.time = t.time;
		if (last_price) statsvc->reportPerformance(tempPr);
		last_price = t.eff_price;
		z.first += t.eff_size;
		//don't calc currency balance on leveraged stocks
		if (minfo.leverage)
			z.second = st.currencyBalance;
		else
			z.second -= t.eff_size * t.eff_price;
		if (!achieve_mode && (cfg.enabled || first_cycle)) {
			auto norm = strategy.onTrade(minfo, t.eff_price, t.eff_size, z.first, z.second);
			trades.push_back(TWBItem(t, last_np+=norm.normProfit, last_ap+=norm.normAccum, norm.neutralPrice));
			lastPriceOffset = t.price - st.spreadCenter;
		} else {
			trades.push_back(TWBItem(t, last_np, last_ap, 0, true));
		}
	}
	walletDB.lock()->alloc(getWalletKey(), strategy.calcCurrencyAllocation(last_price));
	return true;
}

void MTrader::update_dynmult(bool buy_trade,bool sell_trade) {
	dynmult.update(buy_trade, sell_trade);
}

void MTrader::reset() {
	init();
	trades.clear();
	saveState();
}

void MTrader::stop() {
	cfg.enabled = false;
}


void MTrader::repair() {
	init();
	dynmult.setMult(1,1);
	if (cfg.internal_balance) {
		if (!trades.empty())
			internal_balance = std::accumulate(trades.begin(), trades.end(), 0.0, [](double v, const TWBItem &itm) {return v+itm.eff_size;});
	} else {
		internal_balance.reset();
		currency_balance.reset();
	}
	currency_balance.reset();
	strategy.reset();
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
	saveState();
}

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

class ConfigOuput {
public:


	class Mandatory:public ondra_shared::VirtualMember<ConfigOuput> {
	public:
		using ondra_shared::VirtualMember<ConfigOuput>::VirtualMember;
		auto operator[](StrViewA name) const {
			return getMaster()->getMandatory(name);
		}
	};

	Mandatory mandatory;

	class Item {
	public:

		Item(StrViewA name, const ondra_shared::IniConfig::Value &value, std::ostream &out, bool mandatory):
			name(name), value(value), out(out), mandatory(mandatory) {}

		template<typename ... Args>
		auto getString(Args && ... args) const {
			auto res = value.getString(std::forward<Args>(args)...);
			out << name << "=" << res ;trailer();
			return res;
		}

		template<typename ... Args>
		auto getUInt(Args && ... args) const {
			auto res = value.getUInt(std::forward<Args>(args)...);
			out << name << "=" << res;trailer();
			return res;
		}
		template<typename ... Args>
		auto getNumber(Args && ... args) const {
			auto res = value.getNumber(std::forward<Args>(args)...);
			out << name << "=" << res;trailer();
			return res;
		}
		template<typename ... Args>
		auto getBool(Args && ... args) const {
			auto res = value.getBool(std::forward<Args>(args)...);
			out << name << "=" << (res?"on":"off");trailer();
			return res;
		}
		bool defined() const {
			return value.defined();
		}

		void trailer() const {
			if (mandatory) out << " (mandatory)";
			out << std::endl;
		}

	protected:
		StrViewA name;
		const ondra_shared::IniConfig::Value &value;
		std::ostream &out;
		bool mandatory;
	};

	Item operator[](ondra_shared::StrViewA name) const {
		return Item(name, ini[name], out, false);
	}
	Item getMandatory(ondra_shared::StrViewA name) const {
		return Item(name, ini[name], out, true);
	}

	ConfigOuput(const ondra_shared::IniConfig::Section &ini, std::ostream &out)
	:mandatory(this),ini(ini),out(out) {}

protected:
	const ondra_shared::IniConfig::Section &ini;
	std::ostream &out;
};

void MTrader::dropState() {
	storage->erase();
	statsvc->clear();
}

class ConfigFromJSON {
public:

	class Mandatory:public ondra_shared::VirtualMember<ConfigFromJSON> {
	public:
		using ondra_shared::VirtualMember<ConfigFromJSON>::VirtualMember;
		auto operator[](StrViewA name) const {
			return getMaster()->getMandatory(name);
		}
	};

	class Item {
	public:

		json::Value v;

		Item(json::Value v):v(v) {}

		auto getString() const {return v.getString();}
		auto getString(json::StrViewA d) const {return v.defined()?v.getString():d;}
		auto getUInt() const {return v.getUInt();}
		auto getUInt(std::size_t d) const {return v.defined()?v.getUInt():d;}
		auto getNumber() const {return v.getNumber();}
		auto getNumber(double d) const {return v.defined()?v.getNumber():d;}
		auto getBool() const {return v.getBool();}
		auto getBool(bool d) const {return v.defined()?v.getBool():d;}
		bool defined() const {return v.defined();}

	};

	Item operator[](ondra_shared::StrViewA name) const {
		return Item(config[name]);
	}
	Item getMandatory(ondra_shared::StrViewA name) const {
		json::Value v = config[name];
		if (v.defined()) return Item(v);
		else throw std::runtime_error(std::string(name).append(" is mandatory"));
	}

	Mandatory mandatory;

	ConfigFromJSON(json::Value config):mandatory(this),config(config) {}
protected:
	json::Value config;
};



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
	if (cfg.internal_balance) return internal_balance;
	else return std::optional<double>();
}
std::optional<double> MTrader::getInternalCurrencyBalance() const {
	if (cfg.internal_balance) return currency_balance;
	else return std::optional<double>();
}


MTrader::SpreadCalcResult MTrader::calcSpread() const {
	if (chart.size() < 5) return SpreadCalcResult{0,0};
	std::vector<double> values(chart.size());
	std::transform(chart.begin(), chart.end(),  values.begin(), [&](auto &&c) {return c.last;});

	SpreadCalcResult lnspread = stCalcSpread(values.begin(), values.end(), cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours);

	return lnspread;


}

MTrader::VisRes MTrader::visualizeSpread(std::function<std::optional<ChartItem>()> &&source, double sma, double stdev,
		double mult, double dyn_raise, double dyn_fall,
		json::StrViewA dynMode, bool sliding, bool dyn_mult,
		bool strip, bool onlyTrades) {
	DynMultControl dynmult(dyn_raise, dyn_fall, strDynmult_mode[dynMode], dyn_mult);
	VisRes res;
	double last = 0;
	double last_price = 0;
	std::vector<double> prices;
	std::size_t isma = sma*60;
	std::size_t istdev = stdev * 60;
	std::size_t mx = std::max(isma+istdev, 2*istdev);
	for (auto k = source(); k.has_value(); k = source()) {
		double p = k->last;
		if (last || sliding) {
	/*		if (minfo.invert_price) p = 1.0/p;*/
			prices.push_back(p);
			auto spread_info = stCalcSpread(prices.end()-std::min(mx,prices.size()), prices.end(), sma*60, stdev*60);
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

void MTrader::DynMultControl::setMult(double buy, double sell) {
	this->mult_buy = buy;
	this->mult_sell = sell;
}

double MTrader::DynMultControl::getBuyMult() const {
	return mult_buy;
}

double MTrader::DynMultControl::getSellMult() const {
	return mult_sell;
}

double MTrader::DynMultControl::raise_fall(double v, bool israise) {
	if (israise) {
		double rr = raise/100.0;
		return mult?v*(1+rr):v + rr;
	} else {
		double ff = fall/100.0;
		return std::max(1.0,mult?v*(1.0-ff):v - ff);
	}

}

void MTrader::DynMultControl::update(bool buy_trade, bool sell_trade) {

	switch (mode) {
	case Dynmult_mode::disabled:
		mult_buy = 1.0;
		mult_sell = 1.0;
		return;
	case Dynmult_mode::independent:
		break;
	case Dynmult_mode::together:
		buy_trade = buy_trade || sell_trade;
		sell_trade = buy_trade;
		break;
	case Dynmult_mode::alternate:
		if (buy_trade) mult_sell = 0;
		else if (sell_trade) mult_buy = 0;
		break;
	case Dynmult_mode::half_alternate:
		if (buy_trade) mult_sell = ((mult_sell-1) * 0.5) + 1;
		else if (sell_trade) mult_buy = ((mult_buy-1) * 0.5) + 1;
		break;
	}
	mult_buy= raise_fall(mult_buy, buy_trade);
	mult_sell= raise_fall(mult_sell, sell_trade);
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
	internal_balance = assets;
	currency_balance = currency;
}

void MTrader::DynMultControl::reset() {
	mult_buy = 1.0;
	mult_sell = 1.0;
}

void MTrader::activateAchieveMode(double position) {
	strategy.reset();
	auto tk = stock->getTicker(cfg.pairsymb);
	auto cur = stock->getBalance(minfo.currency_symbol, cfg.pairsymb);
	strategy.onIdle(minfo, tk, (minfo.invert_price?-1.0:1.0)*position, cur);
	achieve_mode = true;
	saveState();
}

bool MTrader::checkAchieveModeDone(const Status &st) {
	double eq = strategy.getEquilibrium(st.assetBalance);
	if (!std::isfinite(eq)) return false;
	double low = eq * std::exp(-st.curStep);
	double hi = eq * std::exp(st.curStep);
	return st.curPrice >= low && st.curPrice <=hi;

}

WalletDB::Key MTrader::getWalletKey() const {
	return WalletDB::Key{
		cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid
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
	if (minfo.leverage && cfg.max_leverage
			&& currency_balance.has_value() && order.size * *internal_balance > 0) {
		double bal = *currency_balance;

		if (!trades.empty()) {
			double chng = order.price - trades.back().eff_price;
			bal += chng * *internal_balance;
		}
		double max_bal = bal * cfg.max_leverage;
		double cash_flow = *internal_balance * order.size < 0?
				std::abs((order.size + *internal_balance)  * order.price)
				:std::abs(order.size  * order.price);
		if (cash_flow > max_bal) {
			maxSize = (max_bal / order.price - std::abs(*internal_balance));
			if (maxSize < 0) maxSize = 0;
			maxSize *= sgn(order.size);
			return false;
		}
	}
	return true;

}

void MTrader::checkLeverage(const Order &order) const {
	double d;
	if (!checkLeverage(order, d)) throw std::runtime_error("Over max leverage");
}


