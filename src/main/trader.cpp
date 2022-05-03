/*
 * mtrader3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include <shared/logOutput.h>
#include <imtjson/object.h>
#include <random>
#include "trader.h"

#include <imtjson/string.h>
#include "errhandler.h"

#include "swap_broker.h"

#include "papertrading.h"

#include "sgn.h"
using ondra_shared::logInfo;

#include "../shared/logOutput.h"

PStockApi selectStock(PStockApi s, SwapMode3 swap_mode, bool paper_trading) {

	switch (swap_mode) {
		case SwapMode3::invert: s = std::make_shared<InvertBroker>(s);break;
		case SwapMode3::swap: s = std::make_shared<SwapBroker>(s);break;
		default: break;
	}
	if (paper_trading) {
		auto new_s = std::make_shared<PaperTrading>(s);
		return new_s;
	} else {
		return s;
	}
}


class Trader::Control: public AbstractTraderControl {
public:
	Control(Trader &owner, const MarketState &state);
	virtual const MarketState &get_state() const override;
	const MarketState &get_orig_state() const;
	virtual NewOrderResult alter_position(double new_pos, double price) override;
	virtual NewOrderResult limit_buy(double price, double size) override;
	virtual NewOrderResult limit_sell(double price, double size) override;
	virtual NewOrderResult market_buy(double size) override;
	virtual NewOrderResult market_sell(double size) override;
	virtual void cancel_buy() override;
	virtual void cancel_sell() override;
	virtual void set_equilibrium_price(double price) override;
	virtual void set_safe_range(const MinMax &minmax) override;
	virtual void set_currency_allocation(double allocation) override;
	virtual void set_equity_allocation(double allocation) override;
	virtual void report_neutral_price(double neutral_price) override;
	virtual void report_price(std::string_view title, double value) override;
	virtual void report_position(std::string_view title, double value) override;
	virtual void report_percent(std::string_view title, double value) override;
	virtual void report_percent(std::string_view title, double value, double base) override;
	virtual void report_number(std::string_view title, double value) override;
	virtual void report_string(std::string_view title, std::string_view string) override;
	virtual void report_bool(std::string_view title, bool value) override;
	virtual void report_nothing(std::string_view title) override;
	virtual void set_buy_order_error(std::string_view text, double display_price, double display_size) override;
	virtual void set_sell_order_error(std::string_view text, double display_price, double display_size) override;
	virtual void log(std::string_view text) override;
	virtual double calc_min_size(double price) override;

	void set_std_buy_error(NewOrderResult rs, double price, double size);
	void set_std_sell_error(NewOrderResult rs, double price, double size);

	Trader &owner;
	MarketState state;
	const MarketState &orig_state;
	json::Object rpt;
	std::optional<double> new_neutral_price;
	std::optional<double> eq_allocation;
	std::optional<double> new_equilibrium;
	std::optional<MinMax> safeRange;

	std::optional<LimitOrder> limit_buy_order,limit_sell_order;
	std::optional<double> market_order;

	struct OrderError: public LimitOrder {
		OrderRequestResult reason;
		std::string text;
		std::string_view get_reason() const {
			if (text.empty()) return Strategy3::strOrderRequestResult[reason];
			else return text;
		}
	};

	std::optional<OrderError> buy_error, sell_error;


	bool canceled_buy=false;
	bool canceled_sell=false;

	NewOrderResult checkBuySize(double price, double size);
	NewOrderResult checkSellSize(double price, double size);
};

Trader::Trader(const Trader_Config &cfg, Trader_Env &&env)
:cfg(cfg), env(std::move(env)),pos_diff(false,0,0),acb_state(false,0,0),trarr(*this),inv_trarr(*this),safeRange{0, std::numeric_limits<double>::infinity()} {

	if (env.statsvc != nullptr) {
		magic = env.statsvc->getHash() & 0xFFFFFFFF;
		magic2 = (~magic) & 0xFFFFFFFF;; //magic number for secondary orders
	} else {
		magic = 100;
		magic2 = 101;
	}
	std::random_device rnd;
	uid = 0;
	while (!uid) {
		uid = rnd();
	}

	if (cfg.dont_allocate) {
		//create independed wallet db
		env.walletDB = nullptr;
	}

	if (cfg.hidden) {
		env.statsvc = nullptr;
	}
	this->env.exchange = selectStock(this->env.exchange, cfg.swap_mode, cfg.paper_trading);

}

void Trader::update_minfo() {
	minfo = env.exchange->getMarketInfo(cfg.pairsymb);
	minfo.min_size = std::max(minfo.min_size, cfg.min_size);

	if (env.statsvc != nullptr) {
		env.statsvc->setInfo(
			IStatSvc::Info {
			cfg.title,
			cfg.broker,
			minfo,
			env.exchange,
			cfg.report_order
		});
	}

}


void Trader::init() {
	if (inited) return;

	if (env.statsvc != nullptr) {
		env.statsvc->init();
	}

	update_minfo();

	if (!cfg.dont_allocate || cfg.enabled) {
		if (env.conflicts != nullptr) {
			auto clk = env.conflicts.lock();
			auto r = clk->get(cfg.broker, minfo.wallet_id, cfg.pairsymb);
			if (r != 0 && r != magic) {
				  throw std::runtime_error("Conflict: Can't run multiple traders on a single pair \r\n\r\n(To have a disabled trader on the same pair you have to enable 'No budget allocation' on the disabled trader)");
			}
			clk->put(cfg.broker, minfo.wallet_id, cfg.pairsymb, magic);
		}
	}

	load_state();
	inited = true;

}

void Trader::load_state() {
	if (env.state_storage == nullptr) return;
	json::Value st = env.state_storage->load();
	if (!st.hasValue()) return ;

	env.strategy.load(st["strategy"]);
	env.spread_gen.load(st["spread"]);
	auto state = st["state"];
	if (state.defined()) {
		json::Value tmp;
		trade_lastid = state["trade_lastid"];
		uid = state["uid"].getUInt();
		position_valid = (tmp = state["position"]).defined();
		if (position_valid) position = tmp.getNumber();
		completed_trades = state["completed_trades"].getUInt();
		prevTickerTime = state["prevTickerTime"].getUIntLong();
		unconfirmed_position = state["unconfirmed_position"].getNumber();
		last_known_live_position = state["last_known_live_position"].getNumber();
		last_known_live_balance = state["last_known_live_balance"].getNumber();
		last_trade_price = state["last_trade_price"].getNumber();
		last_trade_size = state["last_trade_size"].getNumber();
		equilibrium=state["equilibrium"].getNumber();

		reset_rev = state["reset_rev"].getUInt();
		json::Value ach = state["achieve"];
		if (ach.type() == json::object) {
			achieve_mode = AchieveMode {
				ach["position"].getNumber(),
				ach["balance"].getNumber()
			};
		} else {
			achieve_mode.reset();
		}
		if ((tmp = state["last_trade_eq_extra"]).defined()) {
			last_trade_eq_extra = tmp.getNumber();
		}
	}

	trades.clear();
	auto tr = st["trades"];
	for (json::Value x: tr) {
		trades.push_back(Trade::fromJSON(x));
	}
	updateEnterPrice();
}

void Trader::save_state() {
	if (env.state_storage == nullptr) return;
	json::Object out;
	out.set("strategy",env.strategy.save());
	out.set("spread", env.spread_gen.save());
	{
		auto state = out.object("state");
		state.set("trade_lastid", trade_lastid);
		state.set("uid",  uid);
		if (position_valid) {
			state.set("position", position);
			state.set("unconfirmed_position", unconfirmed_position);
		}
		state.set("reset_rev", reset_rev);
		state.set("completed_trades", completed_trades);
		state.set("prevTickerTime", prevTickerTime);
		state.set("last_known_live_position", last_known_live_position);
		state.set("last_known_live_balance", last_known_live_balance);
		state.set("last_trade_price",last_trade_price);
		state.set("equilibrium",equilibrium);
		state.set("last_trade_size", last_trade_size);
		if (last_trade_eq_extra.has_value()) {
			state.set("last_trade_eq_extra", *last_trade_eq_extra);
		}
		if (achieve_mode.has_value()) {
			state.set("achieve", json::Object{
				{"position", achieve_mode->position},
				{"balance", achieve_mode->balance},
			});
		}

	}
	out.set("trades", json::Value(json::array, trades.begin(), trades.end(), [&](const Trade &tr){
		return tr.toJSON();
	}));

	env.state_storage->store(out);

}


void Trader::updateEnterPrice() {
	double initState = (position_valid?position:0) - std::accumulate(trades.begin(), trades.end(), 0.0, [&](double a, const auto &tr) {
		return a + tr.eff_size;
	});
	double openPrice = cfg.init_open;
	if (openPrice == 0) {
		if (!trades.empty()) openPrice = trades[0].eff_price;
	} else {
		if (minfo.invert_price) openPrice = 1.0/openPrice;
	}

	ACB acb(minfo.initACB(openPrice, initState));
	spent_currency = 0;
	for (const auto &tr: trades) {
		acb = acb(tr.eff_price, tr.eff_size);
		spent_currency += tr.size * tr.price;
	}
	acb_state = acb;
	ACB diff(minfo.initACB());
	for (std::size_t p = completed_trades, cnt = trades.size(); p != cnt; ++p) {
		const auto &tr = trades[p];
		diff = diff(tr.eff_price, tr.eff_size);
	}
	pos_diff = diff;
}

std::size_t Trader::TradesArray::size() const {
	return owner.completed_trades;
}

TradeRecord Trader::TradesArray::operator [](std::size_t idx) const {
	return owner.trades[idx];
}

const Trader::TradesArrayBase *Trader::TradesArray::invert() const {
	return &owner.inv_trarr;
}


const Trader::TradesArrayBase *Trader::InvTradesArray::invert() const {
	return &owner.trarr;
}

std::size_t Trader::InvTradesArray::size() const {
	return owner.completed_trades;
}

TradeRecord Trader::InvTradesArray::operator [](std::size_t idx) const {
	auto x = owner.trades[idx];;
	x.eff_price = 1.0/x.eff_price;
	x.price = 1.0/x.price;
	x.eff_size = -x.eff_size;
	x.size = -x.size;
	return x;

}

bool Trader::processTrades() {
	auto newtrades = env.exchange->syncTrades(trade_lastid, cfg.pairsymb);
	if (newtrades.trades.empty()) {
		trade_lastid = newtrades.lastId;
		return false;
	}
	double prev_neutral = 0;
	double prev_norm = 0;
	double last_price = 0;
	if (!trades.empty()) {
		prev_neutral = trades.back().neutral_price;
		prev_norm = trades.back().norm_profit;
		last_price = trades.back().eff_price;
	}


	for (const IStockApi::Trade &t: newtrades.trades) {
		pos_diff = pos_diff(t.eff_price, t.eff_size);
		acb_state = acb_state(t.eff_price, t.eff_size);
		double volume = minfo.calcCurrencyChange(t.size,t.price,true);
		spent_currency -= volume;
		trades.push_back(Trade(t, prev_norm, 0, prev_neutral));
		if (env.statsvc != nullptr) {
			env.statsvc->reportPerformance(PerformanceReport{
				magic, uid, t.id.toString().str(), minfo.currency_symbol,
				minfo.asset_symbol,cfg.broker,
				t.price, t.size, last_price?(t.eff_price-last_price)*(acb_state.getPos() - t.eff_price):0,
				acb_state.getPos(),
				minfo.simulator,
				minfo.invert_price,
				t.time,
				acb_state.getRPnL()
			});
		}
		if (achieve_mode.has_value() && !minfo.leverage) {
			achieve_mode->balance += volume;
		}
	}
	trade_lastid = newtrades.lastId;
	return true;
}

void Trader::detect_lost_trades(bool any_trades, const MarketStateEx &mst) {
	if (!any_trades) {
		//detection of lost trades
		if (std::fabs(mst.broker_assets - last_known_live_position)
				> minfo.getMinSize(mst.cur_price)) {
			//possible lost trade
			//for leveraged market, it is always lost trade
			if (minfo.leverage) {
				unconfirmed_position += mst.broker_assets
						- last_known_live_position;
				last_known_live_position = mst.broker_assets;
			} else {
				double pos_change = mst.broker_assets
						- last_known_live_position;
				double balance_change = mst.broker_currency
						- last_known_live_balance;
				last_known_live_position = mst.broker_assets;
				last_known_live_balance = mst.broker_currency;
				double pos_change_2 = -balance_change / mst.cur_price;
				if (std::abs(pos_change - pos_change_2) / pos_change > 0.1) {
					//possible lost trade
					unconfirmed_position += pos_change;
				}
			}
		}
	} else {
		unconfirmed_position = acb_state.getPos();
		//after trade, we reset unconfirmed position
	}
	if (std::abs(unconfirmed_position - acb_state.getPos())
			> minfo.getMinSize(mst.cur_price)) {
		close_all_orders();
		save_state();
		throw std::runtime_error("Lost trade detected");
	}
}


void Trader::reset_broker(const std::chrono::system_clock::time_point &mark) {
	try {
		env.exchange->reset(mark);
	} catch (...) {
		report_exception();
	}
}

void Trader::run() {
	try {
		if (!inited) init();

		openOrderCache.reset();
		schOrders.clear();

		bool any_trades = processTrades();
		bool trades_finished = false;
		if (target_buy) {
			trades_finished = trades_finished || (pos_diff.getPos() >= target_buy);
		}
		if (target_sell) {
			trades_finished = trades_finished ||  (pos_diff.getPos() <= target_sell);
		}
		if (trades_finished) {
			double open = pos_diff.getOpen();
			if (std::isfinite(open)) {
				last_trade_price  = pos_diff.getOpen();
				last_trade_size  = pos_diff.getPos();
				position = position + pos_diff.getPos();
				pos_diff = minfo.initACB(0,0);
				completed_trades = trades.size();
				equilibrium = last_trade_price;
			} else {
				trades_finished = false;
			}
		}

		mst = getMarketState(trades_finished);
		mst_valid = true;
		env.spread_gen = mst.spread_new_state;

		if (env.histData != nullptr) {
			env.histData->store({
				mst.cur_time, mst.cur_price
			});
		}

		if (stopped || !cfg.enabled) {
			stop_trader();
			return;
		}

		if (reset_rev < cfg.reset.revision) {
			do_reset(mst);
		}

		if (!position_valid) throw std::runtime_error("Need reset");


//		detect_lost_trades(any_trades, mst);

		Control cntr(*this,mst);
		bool skip_strategy = false;


		if (achieve_mode.has_value()) {
			if (!do_achieve(*achieve_mode, cntr)) {
				if (achieve_mode->balance < 0) {
					throw std::runtime_error("Achieve has failed - negative available balance - please reset again");
				}
				mst.balance = achieve_mode->balance;
				achieve_mode.reset();
			} else  {
				skip_strategy = true;
			}
		}
		if (!skip_strategy){

			if (first_run) {
				mst.event = MarketEvent::start;
				first_run = false;
			}
			env.strategy.run(cntr);
			if (cntr.eq_allocation.has_value()) {
				eq_allocation = *cntr.eq_allocation;
			}
			if (minfo.leverage) cur_allocation = eq_allocation; else cur_allocation = eq_allocation - mst.position*mst.cur_price;
			if (cntr.new_neutral_price.has_value()) {
				neutral_price = *cntr.new_neutral_price;
			}
			if (cntr.safeRange.has_value()) {
				safeRange = *cntr.safeRange;
			}

			if (mst.event == MarketEvent::trade) {
				double eq_extra = acb_state.getEquity(last_trade_price) - eq_allocation;
				double chng = last_trade_eq_extra.has_value()?eq_extra - *last_trade_eq_extra:0.0;
				last_trade_eq_extra = eq_extra;
				if (!trades.empty()) {
					auto &tb = trades.back();
					tb.norm_profit+=chng;
					neutral_price = tb.neutral_price = cntr.new_neutral_price.has_value()?*cntr.new_neutral_price:0;
				}
				equilibrium = cntr.new_equilibrium.has_value()?*cntr.new_equilibrium:last_trade_price;
			} else {
				if (cntr.new_equilibrium.has_value()) {
					equilibrium = *cntr.new_equilibrium;
				}
			}
		}

		if (env.balanceCache != nullptr) {
			env.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.asset_symbol, mst.broker_assets);
			env.balanceCache.lock()->put(cfg.broker, minfo.wallet_id, minfo.currency_symbol, mst.broker_currency);
		}
		if (env.walletDB != nullptr) {
			env.walletDB.lock()->alloc({cfg.broker, minfo.wallet_id, minfo.currency_symbol, uid},minfo.leverage
					?eq_allocation :eq_allocation-mst.last_trade_price*acb_state.getPos());
			if (!minfo.leverage)  {
				env.walletDB.lock()->alloc({cfg.broker, minfo.wallet_id, minfo.asset_symbol, uid},acb_state.getPos());
			}
		}

		strategy_report_state = cntr.rpt;
		OrderPair cur_orders = fetchOpenOrders(magic);
		placeAllOrders(cntr, cur_orders);

		std::string_view buy_error, sell_error, gen_error;
		newOrders.clear();
		std::transform(schOrders.begin(), schOrders.end(), std::back_inserter(newOrders), [&](const ScheduledOrder &o){
			json::Value replace_id;
			double replace_size =0;
			if (o.size > 0) {
				if (cur_orders.buy.has_value()) {
					replace_id = cur_orders.buy->id;
					replace_size = cur_orders.buy->size;
					cur_orders.buy.reset();
				}
			} else if (o.size < 0) {
				if (cur_orders.sell.has_value()) {
					replace_id = cur_orders.sell->id;
					replace_size = cur_orders.sell->size;
					cur_orders.sell.reset();
				}
			}

			return IStockApi::NewOrder {
				cfg.pairsymb,
				o.size,
				o.price,
				magic,
				replace_id,
				replace_size
			};
		});
		if (!newOrders.empty()) {
			env.exchange->batchPlaceOrder(newOrders, newOrders_ret);
		}
		for (std::size_t cnt = newOrders.size(), i = 0; i < cnt; ++i) {
			if (newOrders_ret[i].error.type() == json::string) {
				if (newOrders[i].size>0) {
					buy_error = newOrders_ret[i].error.getString();
					rej_buy = true;
				} else if (newOrders[i].size<0) {
					sell_error = newOrders_ret[i].error.getString();
					rej_sell = true;
				} else {
					gen_error = newOrders_ret[i].error.getString();
				}
			}
		}

		if (env.statsvc != nullptr) {

			LimitOrder new_buy{0,0}, new_sell{0,0};

			if (cur_orders.buy.has_value()) {
				new_buy = {cur_orders.buy->price, cur_orders.buy->size};
			}

			if (cur_orders.sell.has_value()) {
				new_sell = {cur_orders.sell->price, cur_orders.sell->size};
			}

			if (cntr.buy_error.has_value()) {
				buy_error = cntr.buy_error->get_reason();
				if (cntr.buy_error->price) {
					new_buy = {cntr.buy_error->price,cntr.buy_error->size};
				}
			}
			if (cntr.sell_error.has_value()) {
				buy_error = cntr.sell_error->get_reason();
				if (cntr.sell_error->price) {
					new_sell = {cntr.sell_error->price,cntr.sell_error->size};
				}
			}
			for (const auto &x: schOrders) {
				if (x.size<0) new_sell = {x.price,x.size};
				else if (x.size>0) new_buy = { x.price,x.size};;
			}

			double equity;
			double rel_equity;
			double cur_np;
			if (trades.empty()) {
				equity = rel_equity = cur_np = 0.0;
			} else {
				const auto &b = trades.back();
				equity = acb_state.getEquity(mst.cur_price);
				cur_np = last_trade_eq_extra.has_value()?(equity - eq_allocation) - *last_trade_eq_extra:0.0;
				cur_np += b.norm_profit;
			}


			env.statsvc->reportTrades(acb_state.getPos(),acb_state.isInverted(), trades);
			env.statsvc->reportPrice(mst.cur_price, equity, cur_np);
			env.statsvc->reportOrder(1, new_buy.price, new_buy.size, acb_state.getEquity(new_buy.price), 0);
			env.statsvc->reportOrder(-1, new_sell.price, new_sell.size, acb_state.getEquity(new_sell.price), 0);
			env.statsvc->reportError({gen_error,buy_error, sell_error});
			env.statsvc->reportMisc({
					any_trades?sgn(trades.back().size):0,
					false, //achieve
					cfg.enabled,
					equilibrium,
					env.spread_gen.get_base_spread()*mst.cur_leverage,
					env.spread_gen.get_buy_mult(),
					env.spread_gen.get_sell_mult(),
					safeRange.min,
					safeRange.max,
					eq_allocation,
					1,	//TODO - not used now
					0,  //TODO - not used now
					mst.event_equity - eq_allocation,
					trades.size(),
					trades.empty()?0:trades.back().time - trades[0].time,
					mst.last_trade_price,
					acb_state.getPos(),
					0, //TODO
					0, //TODO
					acb_state.getOpen(),
					acb_state.getRPnL(),
					acb_state.getUPnL(mst.cur_price)
			}, false);
		}

		save_state();

	} catch (...) {
		report_exception();
	}

}

void Trader::report_exception() {
	if (!inited) {	//we need to initialize report class enable appear the trader in the report
		try {
			update_minfo();
		} catch (...) {
			if (env.statsvc != nullptr) {
					env.statsvc->setInfo(
						IStatSvc::Info {
						cfg.title,
						cfg.broker,
						MarketInfo{},
						env.exchange,
						cfg.report_order
					});
				}
			REPORT_UNHANDLED();
		}
	}
	if (env.statsvc) {
		try {
			throw;
		} catch (std::exception &e) {
			env.statsvc->reportError(IStatSvc::ErrorObjEx(e.what()));
		} catch (...) {
			env.statsvc->reportError(IStatSvc::ErrorObjEx("Undetermined error"));
		}
	} else {
		REPORT_UNHANDLED();
	}
}

Trader::MarketStateEx Trader::getMarketState(bool trades_finished) const {
	MarketStateEx mst;
	mst.trades = &trarr;
	mst.minfo = &minfo;
	mst.event = trades_finished?MarketEvent::trade:MarketEvent::idle;

	IStockApi::Ticker ticker = env.exchange->getTicker(cfg.pairsymb);
	mst.cur_price = ticker.last;
	mst.cur_time = ticker.time;
	double eq = equilibrium?equilibrium:last_trade_price?last_trade_price:mst.cur_price;
	if (trades_finished) {
		mst.event = MarketEvent::trade;
		mst.event_price = last_trade_price;
		mst.event_time = trades.back().time;
		mst.spread_new_state = env.spread_gen->report_trade(last_trade_price, last_trade_size);
	} else {
		mst.event = MarketEvent::idle;
		mst.event_price = mst.cur_price;
		mst.event_time = mst.cur_time;
		mst.spread_new_state = env.spread_gen->add_point(mst.cur_price);
	}
	//Remove fees from prices, because fees will be added back later - strategy counts without fees
	mst.highest_buy_price = minfo.priceRemoveFees(minfo.tickToPrice(minfo.priceToTick(ticker.ask)-1),1);
	mst.lowest_sell_price = minfo.priceRemoveFees(minfo.tickToPrice(minfo.priceToTick(ticker.bid)+1),-1);
	mst.sug_buy_price = std::min(mst.highest_buy_price,minfo.priceRemoveFees(mst.spread_new_state->get_order_price(1, eq),1));
	mst.sug_sell_price = std::max(mst.lowest_sell_price,minfo.priceRemoveFees(mst.spread_new_state->get_order_price(-1, eq),-1));

	mst.broker_assets =  env.exchange->getBalance(minfo.asset_symbol, cfg.pairsymb);
	mst.broker_currency =  env.exchange->getBalance(minfo.currency_symbol, cfg.pairsymb);
	mst.position = position;

	double currencyUnadjustedBalance;
	double assetsUnadjustedBalance;
	if (env.externalBalance == nullptr) {

		currencyUnadjustedBalance = mst.broker_currency;
		assetsUnadjustedBalance = mst.broker_assets;
	} else {
		auto extBal = env.externalBalance.lock_shared();
		currencyUnadjustedBalance = mst.broker_currency + extBal->get(cfg.broker, minfo.wallet_id, minfo.currency_symbol);
		assetsUnadjustedBalance = mst.broker_assets + extBal->get(cfg.broker, minfo.wallet_id, minfo.asset_symbol);
	}
	if (env.walletDB == nullptr) {
		mst.balance = currencyUnadjustedBalance;
		mst.avail_assets = assetsUnadjustedBalance;
		mst.live_currencies = mst.broker_currency;
		mst.live_assets = mst.broker_assets;
	} else {
		auto wdb = env.walletDB.lock_shared();
		mst.balance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),	currencyUnadjustedBalance);
		mst.live_currencies = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),mst.broker_currency);
		if (!mst.minfo->leverage) {
			mst.avail_assets = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid), assetsUnadjustedBalance);
			mst.live_assets = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),mst.broker_assets);
		} else {
			mst.live_assets = mst.avail_assets = mst.broker_assets;
		}

	}
	mst.equity = minfo.calcEquity(mst.position,mst.balance, mst.cur_price);
	mst.event_equity = minfo.calcEquity(mst.position, mst.balance, mst.cur_price, mst.event_price);
	mst.last_trade_price = last_trade_price?last_trade_price:mst.cur_price;
	mst.last_trade_size = last_trade_size;
	mst.open_price = acb_state.getOpen();
	mst.rpnl = acb_state.getRPnL();
	mst.upnl = acb_state.getUPnL(mst.cur_price);
	mst.cur_leverage = minfo.calcPosValue(mst.position, mst.cur_price)/mst.equity;

	mst.trade_now = false;
	mst.inverted = false;
	mst.buy_rejected = rej_buy;
	mst.sell_rejected = rej_sell;
	return mst;



}

void Trader::close_all_orders() {
	IStockApi::Orders orders = env.exchange->getOpenOrders(cfg.pairsymb);
	for (const auto &x: orders) {
		if (x.client_id.getUIntLong() == magic || x.client_id.getUIntLong() == magic2) {
			env.exchange->placeOrder(cfg.pairsymb, 0, 0, nullptr, x.id, 0);
		}
	}
}

const MarketState& Trader::Control::get_state() const {
	return state;
}
const MarketState& Trader::Control::get_orig_state() const {
	return orig_state;
}

NewOrderResult Trader::Control::alter_position(double new_pos, double price) {
	double diff = new_pos - state.position;
	if (price <= 0) {
		price = diff < 0?state.sug_sell_price:state.sug_buy_price;
	}
	if (state.inverted) {
		diff = -diff;
		price = 1.0/price;
	}
	NewOrderResult rs;
	if (diff < 0) {
		diff = -diff;
		rs = checkSellSize(price, diff);
		double diff2 = rs.v;
		rs.v = orig_state.minfo->invert_pos_if_needed(orig_state.position - rs.v);
		if (rs.isOk()) {
			limit_sell_order = {price, diff2};
			sell_error.reset();
		} else {
			set_std_sell_error(rs, price, diff);
		}
		return rs;
	} else if (diff > 0){
		rs = checkBuySize(price, diff);
		double diff2 = rs.v;
		rs.v = orig_state.minfo->invert_pos_if_needed(orig_state.position + rs.v);
		if (rs.isOk()) {
			limit_buy_order = {price, diff2};
			buy_error.reset();
		} else {
			set_std_buy_error(rs, price, diff);
		}
		return rs;
	} else {
		return {OrderRequestResult::accepted,0};
	}
}


NewOrderResult Trader::Control::limit_buy(double price, double size) {
	if (state.inverted) {
		price = 1.0/price;
		if (price <= 0) price = state.sug_sell_price;
		NewOrderResult rs = checkSellSize(price, size);
		if (rs.isOk()) {
			limit_sell_order = {price, rs.v};
			sell_error.reset();
		} else {
			set_std_sell_error(rs, price, size);
		}
		return rs;
	} else {
		if (price <= 0) price = state.sug_buy_price;
		NewOrderResult rs = checkBuySize(price, size);
		if (rs.isOk()) {
			limit_buy_order = {price, rs.v};
			buy_error.reset();
		} else {
			set_std_buy_error(rs, price, size);
		}
		return rs;
	}

}

NewOrderResult Trader::Control::limit_sell(double price, double size) {
	if (state.inverted) {
		price = 1.0/price;
		if (price <= 0) price = state.sug_buy_price;
		NewOrderResult rs = checkBuySize(price, size);
		if (rs.isOk()) {
			limit_buy_order = {price, rs.v};
			buy_error.reset();
		} else {
			set_std_buy_error(rs, price, size);
		}
		return rs;
	} else {
		if (price <= 0) price = state.sug_sell_price;
		NewOrderResult rs = checkSellSize(price, size);
		if (rs.isOk()) {
			limit_sell_order = {price, rs.v};
			sell_error.reset();
		}else {
			set_std_sell_error(rs, price, size);
		}
		return rs;
	}

}

NewOrderResult Trader::Control::market_buy(double size) {
	if (state.inverted) {
		NewOrderResult rs = checkSellSize(size, state.lowest_sell_price);
		if (rs.isOk()) market_order = -rs.v;
		return rs;
	} else {
		NewOrderResult rs = checkBuySize(size, state.highest_buy_price);
		if (rs.isOk()) market_order = rs.v;
		return rs;
	}


}

NewOrderResult Trader::Control::market_sell(double size) {
	if (state.inverted) {
		NewOrderResult rs = checkBuySize(size, state.highest_buy_price);
		if (rs.isOk()) market_order = rs.v;
		return rs;
	} else {
		NewOrderResult rs = checkSellSize(size, state.lowest_sell_price);
		if (rs.isOk()) market_order = -rs.v;
		return rs;
	}

}

void Trader::Control::cancel_buy() {
	canceled_buy = true;
	limit_buy_order.reset();
}
void Trader::Control::cancel_sell() {
	canceled_sell = true;
	limit_sell_order.reset();
}

void Trader::Control::set_equilibrium_price(double price) {
	if (std::isfinite(price)) {
		new_equilibrium = state.inverted?1.0/price:price;
	} else {
		throw std::runtime_error("equilibrium is not finite");
	}
}

void Trader::Control::set_safe_range(const MinMax &minmax) {
	if (state.inverted) {
		safeRange = {1.0/minmax.max, 1.0/minmax.min};
	} else {
		safeRange = minmax;
	}
}

void Trader::Control::set_currency_allocation(double allocation) {
	if (std::isfinite(allocation)) {
		eq_allocation = state.inverted
				 ?allocation-state.position/state.cur_price
				 :allocation+state.position*state.cur_price;
	} else {
		throw std::runtime_error("currency allocation is not finite");
	}
}

void Trader::Control::set_equity_allocation(double allocation) {
	if (std::isfinite(allocation)) {
		eq_allocation = allocation;
	} else {
		throw std::runtime_error("equity allocation is not finite");
	}
}

void Trader::Control::report_neutral_price(double neutral_price) {
	if (std::isfinite(neutral_price)) {
		new_neutral_price = state.inverted?1.0/neutral_price:neutral_price;
	} else {
		throw std::runtime_error("neutral price is not finite");
	}
}

void Trader::Control::report_price(std::string_view title, double value) {
	rpt.set(title,state.inverted?1.0/value:value);
}

void Trader::Control::report_position(std::string_view title, double value) {
	rpt.set(title, (state.inverted?-1.0:1.0)*value);
}

void Trader::Control::report_percent(std::string_view title, double value) {
	rpt.set(title, value*100);
}

void Trader::Control::report_percent(std::string_view title, double value, double base) {
	rpt.set(title, value/base*100);
}

void Trader::Control::report_number(std::string_view title, double value) {
	rpt.set(title,value);
}

void Trader::Control::report_string(std::string_view title, std::string_view string) {
	rpt.set(title,string);
}

void Trader::Control::report_bool(std::string_view title, bool value) {
	rpt.set(title,value);
}

void Trader::Control::report_nothing(std::string_view title) {
	rpt.unset(title);
}

Trader::Control::Control(Trader &owner, const MarketState &state)
:owner(owner)
,state(owner.minfo.type == MarketType::inverted
		?invert_market_state(state)
		:state)
,orig_state(state)
,rpt(owner.strategy_report_state)
{
}

NewOrderResult Trader::Control::checkBuySize(double price, double size) {

	if (!std::isfinite(price) || price <= 0 || price > orig_state.highest_buy_price) {
		return {OrderRequestResult::invalid_price,0};
	}
	double minsize = orig_state.minfo->getMinSize(price);
	double sz = size;
	if (sz < minsize) return {OrderRequestResult::too_small, minsize};


	if (owner.cfg.max_position.has_value()) {
		double mp = *owner.cfg.max_position;
		if (orig_state.position > mp) return {OrderRequestResult::max_position,0};
		sz = std::min(sz,mp - orig_state.position);
		if (sz < minsize) return {OrderRequestResult::max_position,0};
	}

	if (owner.cfg.max_costs.has_value()) {
		double mc = *owner.cfg.max_costs;
		double new_spend = owner.spent_currency - orig_state.minfo->calcCurrencyChange(sz, price, true);
		if (new_spend > mc) {
			sz = sz * (mc - owner.spent_currency)/(new_spend - owner.spent_currency);
			if (sz < minsize) return {OrderRequestResult::max_costs,0};
		}
	}

	if (owner.cfg.max_size && sz > owner.cfg.max_size) {
		sz = owner.cfg.max_size;
	}

	if (orig_state.minfo->leverage) {
		if (owner.cfg.max_leverage && orig_state.position >= 0) {
			double teq = orig_state.minfo->calcEquity(orig_state.position, orig_state.balance, orig_state.cur_price, price);
			if (teq<=0) return {OrderRequestResult::max_leverage, 0};
			double val1 = orig_state.minfo->calcPosValue(orig_state.position, price);
			double val2 = orig_state.minfo->calcPosValue(orig_state.position+sz, price);
			double lev1 = val1/teq;
			double lev2 = val2/teq;
			if (lev1 > owner.cfg.max_leverage) return {OrderRequestResult::max_leverage, 0};
			if (lev2 > owner.cfg.max_leverage) {
				double mxval = teq*owner.cfg.max_leverage;
				sz = sz * (mxval - val1)/(val2 - val1);
				if (sz < minsize) return {OrderRequestResult::max_leverage,0};
			}
		}
	} else {
		double chg = orig_state.minfo->calcCurrencyChange(sz, price,true);
		double resbal = orig_state.balance + chg;
		if (resbal < 0) {
			sz = orig_state.minfo->sizeFromCurrencyChange(-orig_state.balance, price);
			if (sz < minsize) return {OrderRequestResult::no_funds, 0};
		}
	}
	if (sz < size) return {OrderRequestResult::partially_accepted, sz};
	else return {OrderRequestResult::accepted, sz};
}

inline void Trader::Control::set_buy_order_error(std::string_view text, double display_price, double display_size) {
	buy_error = {display_price, display_size, OrderRequestResult::accepted, std::string(text)};
	cancel_buy();
}

inline void Trader::Control::set_sell_order_error(std::string_view text, double display_price, double display_size) {
	sell_error = {display_price, display_size, OrderRequestResult::accepted, std::string(text)};
	cancel_sell();
}

inline void Trader::Control::set_std_buy_error(NewOrderResult rs, double price, double size) {
	buy_error = {price,size,rs.state};
	cancel_buy();
}

inline void Trader::Control::set_std_sell_error(NewOrderResult rs, double price, double size) {
	sell_error = {price,size,rs.state};
	cancel_sell();
}

inline void Trader::Control::log(std::string_view text) {
	if (owner.env.statsvc != nullptr) {
		owner.env.statsvc->reportLogMsg(orig_state.cur_time, text);
	}
}

NewOrderResult Trader::Control::checkSellSize(double price, double size) {

	if (!std::isfinite(price) || price < orig_state.lowest_sell_price) {
		return {OrderRequestResult::invalid_price,0};
	}
	double minsize = orig_state.minfo->getMinSize(price);
	double sz = size;
	if (sz < minsize) return {OrderRequestResult::too_small, minsize};


	if (owner.cfg.min_position.has_value()) {
		double mp = *owner.cfg.min_position;
		if (orig_state.position < mp) return {OrderRequestResult::max_position,0};
		sz = std::min(sz,orig_state.position - mp);
		if (sz < minsize) return {OrderRequestResult::max_position,0};
	}

	if (owner.cfg.max_costs.has_value()) {
		double mc = *owner.cfg.max_costs;
		double new_spend = owner.spent_currency - orig_state.minfo->calcCurrencyChange(-sz, price, true);
		if (new_spend > mc) {
			sz = sz * (mc - owner.spent_currency)/(new_spend - owner.spent_currency);
			if (sz < minsize) return {OrderRequestResult::max_costs,0};
		}
	}

	if (owner.cfg.max_size && sz > owner.cfg.max_size) {
		sz = owner.cfg.max_size;
	}

	if (orig_state.minfo->leverage) {
		if (owner.cfg.max_leverage && orig_state.position<=0) {
			double teq = orig_state.minfo->calcEquity(orig_state.position, orig_state.balance, orig_state.cur_price, price);
			if (teq<=0) return {OrderRequestResult::max_leverage, 0};
			double val1 = orig_state.minfo->calcPosValue(orig_state.position, price);
			double val2 = orig_state.minfo->calcPosValue(orig_state.position-sz, price);
			double lev1 = val1/teq;
			double lev2 = val2/teq;
			if (lev1 > owner.cfg.max_leverage) return {OrderRequestResult::max_leverage, 0};
			if (lev2 > owner.cfg.max_leverage) {
				double mxval = teq*owner.cfg.max_leverage;
				sz = sz * (mxval - val1)/(val2 - val1);
				if (sz < minsize) return {OrderRequestResult::max_leverage,0};
			}
		}
	} else {
		double chg = state.minfo->calcCurrencyChange(-sz, price,true);
		double resbal = orig_state.balance + chg;
		if (resbal < 0) {
			sz = orig_state.minfo->sizeFromCurrencyChange(-orig_state.balance, price);
			if (sz < minsize) return {OrderRequestResult::no_funds, 0};
		}
	}
	if (sz < size) return {OrderRequestResult::partially_accepted, sz};
	else return {OrderRequestResult::accepted, sz};

}

bool Trader::isSameOrder(const std::optional<IStockApi::Order> &curOrder, const LimitOrder &newOrder) const {
	if (curOrder.has_value()) {
		return minfo.priceToTick(curOrder->price) == minfo.priceToTick(newOrder.price)
			&& std::abs(curOrder->size - newOrder.size) < minfo.getMinSize(newOrder.price);
	} else {
		return false;
	}

}

Strategy3 Trader::get_strategy() const {
	return env.strategy;
}

SpreadGenerator Trader::get_spread() const {
	return env.spread_gen;
}

json::Value Trader::get_strategy_report() const {
	return strategy_report_state;
}

double Trader::get_strategy_position() const {
	return position;
}

const ACB& Trader::get_position() const {
	return acb_state;
}

const ACB& Trader::get_position_offset() const {
	return pos_diff;
}

double Trader::get_equity_allocation() const {
	return eq_allocation;
}

double Trader::get_currency_allocation() const {
	return cur_allocation;
}

double Trader::get_equilibrium() const {
	return equilibrium;
}

double Trader::get_neutral_price() const {
	return neutral_price;
}


double Trader::get_last_trade_eq_extra() const {
	return last_trade_eq_extra.has_value()?*last_trade_eq_extra:0;
}

void Trader::placeAllOrders(const Control &cntr, const OrderPair &pair) {

	auto buy_order = cntr.limit_buy_order;
	auto sell_order = cntr.limit_sell_order;
	bool cancel_buy = cntr.canceled_buy;
	bool cancel_sell = cntr.canceled_sell;
	auto market_order = cntr.market_order;


	target_buy = target_sell = 0;
	//buy order ----------------
	//buy order has been placed
	if (buy_order.has_value()) {
		//pick order
		LimitOrder b = *buy_order;
		//set target buy size
		target_buy = b.size;
		//decrease size by already realized size
		b.size -= pos_diff.getPos();
		//if final size is zero or negative
		if (b.size <= 0) {
			//cancel that order, don't place new one. On next cycle, trade will be reported
			schOrders.cancel_order(pair.buy);
		} else {
			//add fees to the order
			minfo.addFees(b.size,b.price);
			//if final size is below min size
			if (b.size < minfo.getMinSize(b.price)) {
				//if there is active order, keep it active, otherwise, enforce trade on next cycle
				if (!pair.buy.has_value()) {
					//set target buy
					target_buy = pos_diff.getPos();
				}
				//check whether active order is same
			}else  if (!isSameOrder(pair.buy, b)) {
				//only if not same, replace the order
				schOrders.place_limit(b.price, b.size, pair.buy);
			}
		}
		//order was not placed, check for cancel
	} else if (cancel_buy) {
		//if canceled, cancel current order
		schOrders.cancel_order(pair.buy);
		//if there positive position offset
		if (pos_diff.getPos() > 0) {
			//this will be reported as trade on next cycle
			target_buy = pos_diff.getPos();
		}
		//no order placed and there is no active order
	} else if (!pair.buy.has_value()) {
		//if there positive position offset
		if (pos_diff.getPos() > 0) {
			//this will be reported as trade on next cycle
			target_buy = pos_diff.getPos();
		}
	}

	// sell order ------------
	// request to place sell order
	if (sell_order.has_value()) {
		//pick order
		LimitOrder s = *sell_order;
		//set target sell size
		target_sell = -s.size;
		//increase size by already realized size
		s.size += pos_diff.getPos();
		//if final size is zero or negative
		if (s.size <= 0) {
			//cancel that order, don't place new one. On next cycle, trade will be reported
			schOrders.cancel_order(pair.sell);
		} else {
			//because it is sell order, sign is changed
			s.size = -s.size;
			//add fees to the order
			minfo.addFees(s.size,s.price);
			//if final size is below min size
			if (s.size > -minfo.getMinSize(s.price)) {
				//if there is active order, keep it active, otherwise, enforce trade on next cycle
				if (!pair.sell.has_value()) {
					//set target sell
					target_sell = pos_diff.getPos();
				}
				//check whether active order is same
			}else  if (!isSameOrder(pair.sell, s)) {
				//only if not same, replace the order
				schOrders.place_limit(s.price, s.size, pair.sell);
			}
		}
		//order was not placed, check for cancel
	} else if (cancel_sell) {
		//if canceled, cancel current order
		schOrders.cancel_order(pair.sell);
		//if there positive position offset
		if (pos_diff.getPos() < 0) {
			//this will be reported as trade on next cycle
			target_sell = pos_diff.getPos();
		}
		//no order placed and there is no active order
	} else if (!pair.sell.has_value()) {
		//if there positive position offset
		if (pos_diff.getPos() < 0) {
			//this will be reported as trade on next cycle
			target_sell = pos_diff.getPos();
		}
	}
	if (market_order.has_value()) {
		schOrders.place_market(*market_order);
	}
}

Trader::OrderPair Trader::fetchOpenOrders(std::size_t magic) {
	if (!openOrderCache.has_value()) {
		openOrderCache = env.exchange->getOpenOrders(cfg.pairsymb);
	}
	OrderPair res;
	for (const IStockApi::Order &x: *openOrderCache) {
		if (x.client_id.getUIntLong() == magic) {
			if (x.size > 0) {
				if (res.buy.has_value()) {
					logInfo("Multiple buy orders: $1 $2 - trying to cancel", x.price, x.size);
					schOrders.push_back({0,0,x.id,0});
				} else {
					res.buy = x;
				}
			} else {
				if (res.sell.has_value()) {
					logInfo("Multiple sell orders: $1 $2 - trying to cancel", x.price, x.size);
					schOrders.push_back({0,0,x.id,0});
				} else {
					res.sell = x;
				}
			}
		}
	}
	return res;
}

void Trader::do_reset(MarketStateEx &st) {
	auto alloc_pos = cfg.reset.alloc_position;
	if (!alloc_pos.has_value() && !position_valid) {
		if (minfo.type == MarketType::inverted) {
			alloc_pos = st.avail_assets;
		} else {
			alloc_pos = std::max(st.avail_assets,0.0);
		}
	}

	if (alloc_pos.has_value()) {
		if (position_valid) {
			double dff = *alloc_pos - position;
			if (std::abs(dff)>minfo.asset_step) {
				trades.push_back(Trade(IStockApi::Trade{
					st.cur_time,st.cur_time,dff,st.cur_price,dff,st.cur_price,nullptr
				},0,0,0,true));
			}
			acb_state = acb_state(st.cur_price, dff);
			pos_diff = minfo.initACB(0,0);
			position = *alloc_pos;
		} else {
			position = *alloc_pos;
			position_valid = true;
			double openPrice = cfg.init_open;
			if (openPrice <= 0) {
				if (minfo.invert_price) openPrice = 1.0/st.cur_price;
				else openPrice =st.cur_price;
			}
			acb_state = minfo.initACB(openPrice, position);
			pos_diff = minfo.initACB(0,0);
		}
	}
	st.position = position;
	if (cfg.reset.alloc_currency.has_value()) {
		st.balance = *cfg.reset.alloc_currency;
	}
	env.strategy.reset();
	reset_rev = cfg.reset.revision;

	if (cfg.reset.trade_position.has_value()) {
		achieve_mode = AchieveMode{
			*cfg.reset.trade_position,
			st.balance
		};
	}
	if (cfg.reset.trade_optimal_position) {
		achieve_mode = AchieveMode {
			env.strategy.calc_initial_position(create_initial_state(st)),
			st.balance
		};

	}
	st.event_equity = st.equity = minfo.calcEquity(st.position, st.balance, st.cur_price);
}

PStockApi Trader::get_exchange() const {
	return env.exchange;
}

IStockApi::MarketInfo Trader::get_market_info() const {
	return minfo;
}


void Trader::stop() {
	stopped= true;
}

void Trader::erase_state() {
	if (env.state_storage != nullptr) {
		env.state_storage->erase();
		env.state_storage = nullptr;
	}
}

bool Trader::do_achieve(const AchieveMode &ach, Control &cntr) {
	const MarketState &state = cntr.get_state();
	double diff = ach.position - state.position;
	///difference is less then minimal size, we reached our target
	if (std::abs(diff) < 2*cntr.calc_min_size(state.cur_price)) return false;

	cntr.cancel_buy();
	cntr.cancel_sell();
	if (diff < 0) cntr.limit_sell(state.lowest_sell_price,-diff);
	else cntr.limit_buy(state.highest_buy_price,diff);
	return true;
}

MarketState Trader::get_market_state() const {
	if (mst_valid) return mst;
	else return getMarketState(false);
}

std::size_t Trader::get_incomplete_trade_count() const {
	return trades.size() - completed_trades;
}

void Trader::stop_trader() {
	try {
		auto orders = env.exchange->getOpenOrders(cfg.pairsymb);
		for (const IStockApi::Order &z: orders) {
			if (z.client_id == magic || z.client_id == magic2) {
				env.exchange->placeOrder(cfg.pairsymb, 0, 0, json::Value(), z.id, 0);
			}
		}
		env.statsvc->reportError(IStatSvc::ErrorObjEx("Stopped"));
		env.statsvc->reportOrder(1, 0,0,0,0);
		env.statsvc->reportOrder(-1, 0,0,0,0);
	} catch (...) {
		report_exception();
	}
}

const PHistStorage<HistMinuteDataItem> &Trader::get_minute_chart() const {
	return env.histData;
}

MarketState Trader::invert_market_state(const MarketState &st) {
	const auto *tr = dynamic_cast<const TradesArrayBase *>(st.trades);
	const AbstractTradeList *trr;
	if (tr) {
		trr = tr->invert();
	} else {
		trr = st.trades;
	}

	return {
		st.minfo,
		trr,
		st.event,
		st.cur_time,
		st.event_time,
		-st.position,
		st.balance,
		st.equity,
		1.0/st.open_price,
		st.cur_leverage,
		1.0/st.cur_price,
		1.0/st.event_price,
		st.event_equity,
		1.0/st.highest_buy_price,
		1.0/st.lowest_sell_price,
		1.0/st.sug_sell_price,
		1.0/st.sug_buy_price,
		1.0/st.last_trade_price,
		-st.last_trade_size,
		-st.live_assets,
		st.live_currencies,
		st.rpnl,
		st.upnl,
		st.sell_rejected,
		st.buy_rejected,
		st.trade_now,
		!st.inverted
	};
}

double Trader::Control::calc_min_size(double price) {
	switch (state.minfo->type) {
	default:
	case MarketType::normal: return state.minfo->getMinSize(price);break;
	case MarketType::inverted:return state.minfo->getMinSize(1.0/price);break;
	}
}

InitialState Trader::create_initial_state(const MarketState &st) {
	switch (st.minfo->type) {
	default:
	case MarketType::normal:
		return {
			st.minfo, st.position, st.balance, st.equity, st.cur_price, st.inverted
		};
	case MarketType::inverted:
		return {
			st.minfo, -st.position, st.balance, st.equity, 1.0/st.cur_price, !st.inverted
		};
	}
}
