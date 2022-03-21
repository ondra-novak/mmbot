/*
 * mtrader3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <random>
#include "trader.h"

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
		unconfirmed_position_diff = state["unconfirmed_position"].getNumber();
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
		state.set("unconfirmed_position", unconfirmed_position_diff);
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

void Trader::processTrades() {
	auto newtrades = env.exchange->syncTrades(trade_lastid, cfg.pairsymb);
	double prev_neutral = 0;
	if (!trades.empty()) {
		prev_neutral = trades.back().neutral_price;
	}

	for (const IStockApi::Trade &t: newtrades.trades) {
		pos_diff = pos_diff(t.eff_price, t.eff_size);
		acb_state = acb_state(t.eff_price, t.eff_size);
		spent_currency += t.size * t.price;
		trades.push_back(Trade(t, 0, 0, prev_neutral));
	}
	trade_lastid = newtrades.lastId;
}

MarketState Trader::getMarketState() {
	MarketState mst;
	mst.trades = &trarr;
	mst.minfo = &minfo;
	mst.event = MarketEvent::idle;

	IStockApi::Ticker ticker = env.exchange->getTicker(cfg.pairsymb);
	mst.cur_price = ticker.last;
	mst.timestamp = ticker.time;



}
