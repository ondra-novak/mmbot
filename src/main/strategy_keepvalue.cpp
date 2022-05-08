/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

std::string_view Strategy3_KeepValue::id = "kv";

Strategy3_KeepValue::Strategy3_KeepValue() {}

Strategy3_KeepValue::Strategy3_KeepValue(const State &st):st(st) {}


PStrategy3 Strategy3_KeepValue::run(AbstractTraderControl &cntr) const {
	const MarketState &mst = cntr.get_state();
	if (st.k <= 0) { //if constant is zero, not inited
		State st = calcConstant(mst.cur_price, mst.equity, mst.position);
		return (new Strategy3_KeepValue(st))->run(cntr);
	}
	for (double p: {mst.sug_buy_price, mst.sug_sell_price}) {
		auto r = cntr.alter_position(calcPos(st, p),p);
		if (r.state == OrderRequestResult::too_small) cntr.alter_position(r.v, calcPriceFromPos(st, r.v));
	}
	cntr.set_equilibrium_price(calcPriceFromPos(st, mst.position));
	double budget = calcBudget( st, mst.event_price);
	double minpos = std::max(cntr.calc_min_size(mst.cur_price), mst.position - mst.live_assets);
	cntr.set_equity_allocation(budget);
	if (mst.leveraged) {
		cntr.set_safe_range({
			calcPriceFromEquity(st, budget-mst.live_currencies),
			calcPriceFromPos(st, minpos)
		});
	} else {
		double alloc = budget - st.k;
		cntr.set_safe_range({
			calcPriceFromCurrency(st, alloc-mst.live_currencies),
			calcPriceFromPos(st, minpos)
		});

	}
	return PStrategy3(this);


}

double Strategy3_KeepValue::calcPos(const State &st, double price) {
	return st.k/price;
}

double Strategy3_KeepValue::calcBudget(const State &st, double price) {
	return st.k * std::log(price/st.k)+ st.c;
}

double Strategy3_KeepValue::calcAlloc(const State &st, double price) {
	return st.k * std::log(price/st.k)+ st.c - st.k;
}
double Strategy3_KeepValue::calcPriceFromPos(const State &st, double pos) {
	return st.k/pos;
}

double Strategy3_KeepValue::calcPriceFromCurrency(const State &st, double cur) {
	return st.k*std::exp(cur/st.k-st.c/st.k + 1);
}
double Strategy3_KeepValue::calcPriceFromEquity(const State &st, double equity) {
	return st.k*std::exp(equity/st.k-st.c/st.k);
}

ChartPoint Strategy3_KeepValue::get_chart_point(double price) const {
	return ChartPoint {
		true,
		calcPos(st, price),
		calcBudget(st, price)
	};
}

json::Value Strategy3_KeepValue::save() const {
	json::Value r(json::object);
	r.setItems({
		{"c", st.c},
		{"k", st.k}
	});
	return r;
}

PStrategy3 Strategy3_KeepValue::load(const json::Value &state) const {
	State nwst{
		state["k"].getNumber(),
		state["c"].getNumber()
	};
	return new Strategy3_KeepValue(nwst);
}

double Strategy3_KeepValue::calc_initial_position(const InitialState &st) const {
	if (st.leveraged) return st.equity/st.cur_price;
	return (st.equity*0.59)/st.cur_price;
}

PStrategy3 Strategy3_KeepValue::reset() const {
	return new Strategy3_KeepValue();
}

void Strategy3_KeepValue::reg(AbstractStrategyRegister &r) {
	r.reg_tool({
		std::string(id),"Keep Value","spot",json::Value(json::array)
	}) >> [](json::Value config) {
		return new Strategy3_KeepValue();
	};
}

Strategy3_KeepValue::State Strategy3_KeepValue::calcConstant(double price, double eq, double position) {
	double k = position * price;
	double ref = calcBudget({k,0}, price);
	return State {k,eq-ref};
}
