/*
 * strategy_pile.cpp
 *
 *  Created on: 1. 10. 2021
 *      Author: ondra
 */

#include <imtjson/object.h>
#include "strategy_pile.h"

#include <cmath>

ChartPoint Strategy3_Pile::get_chart_point(double price) const {
	return ChartPoint{
		true,
		calcPos(ratio, constant, price),
		calcBudget(ratio, constant, price)
	};
}

PStrategy3 Strategy3_Pile::run(AbstractTraderControl &cntr) const {
	const MarketState &st = cntr.get_state();
	if (constant <= 0) { //if constant is zero, not inited
		double c = calcConstant(ratio, st.cur_price, st.equity);
		if (c <= 0) throw std::runtime_error("Strategy - failed initialize");
		return (new Strategy3_Pile(ratio,c))->run(cntr);
	}
	for (double p: {st.sug_buy_price, st.sug_sell_price}) {
		auto r = cntr.alter_position(calcPos(ratio, constant, p),p, calcBudget(ratio, constant, p));
		if (r.state == OrderRequestResult::too_small) {
			double pp = calcPriceFromPos(ratio, constant, r.v);
			cntr.alter_position(r.v, pp, calcBudget(ratio, constant, pp));
		}
	}
	cntr.set_equilibrium_price(calcPriceFromPos(ratio, constant, st.position));
	double alloc = calcAlloc(ratio, constant, st.last_trade_price);
	double budget = calcBudget(ratio, constant, st.event_price);
	cntr.set_equity_allocation(budget);
	cntr.set_safe_range({
		st.live_currencies>=alloc
			?0:calcPriceFromCurrency(ratio, constant, alloc-st.live_currencies),
		st.live_assets>=st.position
			?std::numeric_limits<double>::infinity()
			:calcPriceFromPos(ratio, constant, st.position-st.live_assets)
	});
	return PStrategy3(this);
}

json::Value Strategy3_Pile::save() const {
	return json::Object{
		{"constant",constant},
		{"ratio", (int)ratio}
	};
}

PStrategy3 Strategy3_Pile::load(const json::Value &state) const {
	double c= state["constant"].getNumber();
	int d= state["ratio"].getInt();
	if (d != (int)ratio) c = 0;
	return new Strategy3_Pile(ratio, c);
}

Strategy3_Pile::Strategy3_Pile(double ratio):ratio(ratio), constant(-1) {
}

Strategy3_Pile::Strategy3_Pile(double ratio, double constant):ratio(ratio), constant(constant) {
}

double Strategy3_Pile::calcPos(double z, double k, double x) {
	return k * std::pow(x,z-1);
}

double Strategy3_Pile::calcBudget(double z, double k, double x) {
	return calcPos(z,k,x)*x/z;
}

double Strategy3_Pile::calcAlloc(double z, double k, double x) {
	return calcBudget(z, k, x) - x * calcPos(z, k, x);
}

double Strategy3_Pile::calcPriceFromPos(double z, double k, double p) {
	return std::pow(p/k,1.0/(-1 + z));
}

double Strategy3_Pile::calcPriceFromCurrency(double z, double k, double q) {
	return std::pow((k/z - k)/q,-1.0/z);
}

double Strategy3_Pile::calc_initial_position(const InitialState &st) const {
	return (st.equity * ratio)/st.cur_price;
}

std::string_view Strategy3_Pile::get_id() const {return id;}

void Strategy3_Pile::reg(AbstractStrategyRegister &r) {
	static auto def = json::Value::fromString(R"json(
			[{"name":"ratio",
	          "type":"slider",
	          "min":0,
	          "max":100,
	          "step":1,
	          "decimals":0,
			  "default":50
			}])json");

	r.reg_tool({
		std::string(Strategy3_Pile::id),
		"Pile Strategy",
		"spot",def

	}) >> [](json::Value cfg) {
		double ratio = cfg["ratio"].getNumber()*0.01;
		return new Strategy3_Pile(ratio);
	};
}

double Strategy3_Pile::calcConstant(double r, double price, double eq) {
	return eq*r/price * std::pow(price,1-r);
}

std::string_view Strategy3_Pile::id = "pile3";

PStrategy3 Strategy3_Pile::reset() const {
	return new Strategy3_Pile(ratio);
}
