/*
 * mtrader3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <random>
#include "trader.h"

class Trader::Control: public AbstractTraderControl {
public:
	Control(Trader &owner, const MarketState &state);
	virtual const MarketState &get_state() const override;
	virtual NewOrderResult alter_position(double new_pos, double price) override;
	virtual NewOrderResult alter_position_market(double new_pos) override;
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
protected:
	Trader &owner;
	const MarketState &state;
	json::Object rpt;

};

Trader::Trader(const Trader_Config &cfg, Trader_Env &&env)
:cfg(cfg), env(std::move(env)),pos_diff(0,0),acb_state(0,0),trarr(*this) {

	magic = env.statsvc->getHash() & 0xFFFFFFFF;
	magic2 = (~magic) & 0xFFFFFFFF;; //magic number for secondary orders
	std::random_device rnd;
	uid = 0;
	while (!uid) {
		uid = rnd();
	}

	if (cfg.dont_allocate) {
		//create independed wallet db
		env.walletDB = env.walletDB.make();
	}

}

void Trader::update_minfo() {
	minfo = env.exchange->getMarketInfo(cfg.pairsymb);
	minfo.min_size = std::max(minfo.min_size, cfg.min_size);

	if (!cfg.hidden) {
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

	update_minfo();

	if (!cfg.dont_allocate || cfg.enabled) {
		auto clk = env.conflicts.lock();
		auto r = clk->get(cfg.broker, minfo.wallet_id, cfg.pairsymb);
		if (r != 0 && r != magic) {
		      throw std::runtime_error("Conflict: Can't run multiple traders on a single pair \r\n\r\n(To have a disabled trader on the same pair you have to enable 'No budget allocation' on the disabled trader)");
		}
		clk->put(cfg.broker, minfo.wallet_id, cfg.pairsymb, magic);
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
		completted_trades = state["confirmed_trades"].getUInt();
		prevTickerTime = state["prevTickerTime"].getUIntLong();
		unconfirmed_position = state["unconfirmed_position"].getNumber();
		last_known_live_position = state["last_known_live_position"].getNumber();
		last_known_live_balance = state["last_known_live_balance"].getNumber();
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
		if (position_valid) state.set("position", position);
		state.set("confimed_trades", completted_trades);
		state.set("prevTickerTime", prevTickerTime);
		state.set("unconfirmed_position", unconfirmed_position);
		state.set("last_known_live_position", last_known_live_position);
		state.set("last_known_live_balance", last_known_live_balance);
	}
	out.set("trades", json::Value(json::array, trades.begin(), trades.end(), [&](const Trade &tr){
		return tr.toJSON();
	}));

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

	ACB acb(openPrice, initState);
	spent_currency = 0;
	for (const auto &tr: trades) {
		acb = acb(tr.eff_price, tr.eff_size);
		spent_currency += tr.size * tr.price;
	}
	acb_state = acb;
	ACB diff(0,0);
	for (std::size_t p = completted_trades, cnt = trades.size(); p != cnt; ++p) {
		const auto &tr = trades[p];
		diff = diff(tr.eff_price, tr.eff_size);
	}
	pos_diff = diff;
}

std::size_t Trader::TradesArray::size() const {
	return owner.completted_trades;
}

IStockApi::Trade Trader::TradesArray::operator [](std::size_t idx) const {
	return owner.trades[idx];
}

bool Trader::processTrades() {
	auto newtrades = env.exchange->syncTrades(trade_lastid, cfg.pairsymb);
	double prev_neutral = 0;
	if (!trades.empty()) {
		prev_neutral = trades.back().neutral_price;
	}  else {
		return false;
	}

	for (const IStockApi::Trade &t: newtrades.trades) {
		pos_diff = pos_diff(t.eff_price, t.eff_size);
		acb_state = acb_state(t.eff_price, t.eff_size);
		spent_currency += t.size * t.price;
		trades.push_back(Trade(t, 0, 0, prev_neutral));
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

void Trader::run() {
	try {
		if (!inited) init();

		bool any_trades = processTrades();
		MarketStateEx mst = getMarketState();

		detect_lost_trades(any_trades, mst);

	} catch (std::exception &e) {
		if (!cfg.hidden) {
			std::string error;
			error.append(e.what());
			env.statsvc->reportError(IStatSvc::ErrorObj(error.c_str()));
		}
	}

}

Trader::MarketStateEx Trader::getMarketState() {
	MarketStateEx mst;
	mst.trades = &trarr;
	mst.minfo = &minfo;
	mst.event = MarketEvent::idle;

	IStockApi::Ticker ticker = env.exchange->getTicker(cfg.pairsymb);
	mst.cur_price = ticker.last;
	mst.timestamp = ticker.time;
	//Remove fees from prices, because fees will be added back later - strategy counts without fees
	mst.highest_buy_price = minfo.priceRemoveFees(minfo.tickToPrice(minfo.priceToTick(ticker.ask)-1),1);
	mst.lowest_sell_price = minfo.priceRemoveFees(minfo.tickToPrice(minfo.priceToTick(ticker.bid)+1),-1);
	mst.opt_buy_price = minfo.priceRemoveFees(env.spread_gen.get_order_price(1, equilibrium),1);
	mst.opt_sell_price = minfo.priceRemoveFees(env.spread_gen.get_order_price(1, equilibrium),-1);
	mst.buy_rejected = rej_buy;
	mst.sell_rejected = rej_sell;

	mst.broker_assets =  env.exchange->getBalance(minfo.asset_symbol, cfg.pairsymb);
	mst.broker_currency =  env.exchange->getBalance(minfo.currency_symbol, cfg.pairsymb);
	mst.position = position;

	auto extBal = env.externalBalance.lock_shared();

	double currencyUnadjustedBalance = mst.live_currencies + extBal->get(cfg.broker, minfo.wallet_id, minfo.currency_symbol);
	auto wdb = env.walletDB.lock_shared();
	mst.balance = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),	currencyUnadjustedBalance);
	mst.live_currencies = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.currency_symbol,uid),mst.broker_currency);
	mst.live_assets = wdb->adjBalance(WalletDB::KeyQuery(cfg.broker,minfo.wallet_id,minfo.asset_symbol,uid),mst.broker_assets);
	mst.equity = minfo.leverage? mst.balance:mst.cur_price * mst.position + mst.balance;
	mst.last_trade_price = last_trade_price;
	mst.last_trade_size = last_trade_size;
	mst.open_price = acb_state.getOpen();
	mst.rpnl = acb_state.getRPnL();
	mst.upnl = acb_state.getUPnL(mst.cur_price);
	mst.cur_leverage = mst.position*mst.cur_price/mst.equity;
	mst.trade_now = false;

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
}

NewOrderResult Trader::Control::alter_position(double new_pos,
		double price) {
}

NewOrderResult Trader::Control::alter_position_market(double new_pos) {
}

NewOrderResult Trader::Control::limit_buy(double price, double size) {
}

NewOrderResult Trader::Control::limit_sell(double price, double size) {
}

NewOrderResult Trader::Control::market_buy(double size) {
}

NewOrderResult Trader::Control::market_sell(double size) {
}

void Trader::Control::cancel_buy() {
}

void Trader::Control::cancel_sell() {
}

void Trader::Control::set_equilibrium_price(double price) {
}

void Trader::Control::set_safe_range(const MinMax &minmax) {
}

void Trader::Control::set_currency_allocation(double allocation) {
}

void Trader::Control::set_equity_allocation(double allocation) {
}

void Trader::Control::report_neutral_price(double neutral_price) {
}

void Trader::Control::report_price(std::string_view title, double value) {
}

void Trader::Control::report_position(std::string_view title, double value) {
}

void Trader::Control::report_percent(std::string_view title, double value) {
}

void Trader::Control::report_percent(std::string_view title, double value, double base) {
}

void Trader::Control::report_number(std::string_view title, double value) {
}

void Trader::Control::report_string(std::string_view title, std::string_view string) {
}

void Trader::Control::report_bool(std::string_view title, bool value) {
}

void Trader::Control::report_nothing(std::string_view title) {
}

inline Trader::Control::Control(Trader &owner, const MarketState &state)
:owner(owner),state(state),rpt(owner.strategy_report_state)
{
}
