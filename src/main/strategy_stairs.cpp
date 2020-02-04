/*
 * strategy_stairs.cpp
 *
 *  Created on: 2. 2. 2020
 *      Author: ondra
 */

#include "strategy_stairs.h"

#include <cmath>
#include "../imtjson/src/imtjson/object.h"
#include "sgn.h"
Strategy_Stairs::~Strategy_Stairs() {
	// TODO Auto-generated destructor stub
}

intptr_t Strategy_Stairs::getNextStep(double dir) const {
	auto cs = st.step;
	if (cfg.close_on_reverse && dir * cs < 0) {
		if (cfg.zero_step) return 0;
		cs = 0;
	}
	if (dir < 0) {
		cs = cs -1;
		if (cs == 0 && !cfg.zero_step) cs = cs - 1;
		else if (cs < -cfg.max_steps) cs = -cfg.max_steps;
	}
	else if (dir > 0) {
		cs = cs + 1;
		if (cs == 0 && !cfg.zero_step) cs = cs + 1;
		else if (cs > cfg.max_steps) cs = cfg.max_steps;
	}
	return cs;
}

double Strategy_Stairs::calcPattern(std::intptr_t step) const {
	std::uintptr_t s = std::abs(step);
	std::intptr_t dir = sgn(step);
	double sum = 0;

	switch (cfg.pattern) {
	default:
	case constant: return step;
	case harmonic: {
		for (std::uintptr_t i = 0; i < s; i++) {
			sum = sum + 1.0/(i+1.0);
		}
		return sum*dir;
	}
	case arithmetic: {
		for (std::uintptr_t i = 0; i < s; i++) {
			sum = sum + (i+1.0);
		}
		return sum*dir;
	}
	case exponencial: {
		double a = 1;
		for (std::uintptr_t i = 0; i < s; i++) {
			sum = a;
			a = a + a;
		}
		return sum*dir;
	}
	}
}

IStrategy::OrderData Strategy_Stairs::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency) const {

	double power = st.power;
	auto step = getNextStep(dir);
	double new_pos = power * calcPattern(step);
	return OrderData{0.0,calcOrderSize(posToAssets(st.pos),assets, posToAssets(	new_pos))};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Stairs::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return onIdle(minfo,{tradePrice,tradePrice,tradePrice,0},assetsLeft,currencyLeft)
						->onTrade(minfo,tradePrice,tradeSize,assetsLeft,currencyLeft);
	double dir = sgn(tradeSize);
	double power = st.power;
	double curpos = assetsToPos(assetsLeft);
	double prevpos = curpos - tradeSize;
	intptr_t step = getNextStep(dir);
	State nst = st;
	nst.price = tradePrice;
	nst.pos = power * calcPattern(step);
	nst.open = curpos*prevpos <=0 ? tradePrice:(st.open*prevpos + tradeSize*tradePrice)/curpos;
	nst.step = step;
	double spread = std::abs(tradePrice - st.price);
	double astp = std::abs(nst.step);
	nst.value = power * spread * astp* (astp+1) / 2;
	if (minfo.leverage) {
		nst.power = calcPower(tradePrice, currencyLeft);
	} else {
		nst.neutral_pos = calcNeutralPos(assetsLeft,currencyLeft, tradePrice);
		nst.power = calcPower(tradePrice, nst.neutral_pos * tradePrice);
	}
	return {
		OnTradeResult{
			prevpos * (tradePrice - st.price) + (nst.value - st.value),
			0,tradePrice - spread*nst.step,nst.open
		},new Strategy_Stairs(cfg,nst)
	};
}

IStrategy::MinMax Strategy_Stairs::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {
	return MinMax {
		0, std::numeric_limits<double>::infinity()
	};
}

bool Strategy_Stairs::isValid() const {
	return st.price > 0 && st.power > 0;
}

json::Value Strategy_Stairs::exportState() const {
	return json::Object("price", st.price)
					   ("pos", st.pos)
					   ("open", st.open)
					   ("value", st.value)
					   ("neutral_pos", st.neutral_pos)
					   ("power", st.power)
					   ("step", st.step);
}

double Strategy_Stairs::calcPower(double price, double currency) const {
	double steps = calcPattern(cfg.max_steps);
	return (currency / price * std::pow(10, cfg.power)*0.01)/steps;
}

PStrategy Strategy_Stairs::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		if (minfo.leverage) {
			double ps = calcPower(curTicker.last, currency);
			return new Strategy_Stairs(cfg, State {curTicker.last, 0, curTicker.last, 0, 0, ps,  0});
		} else {
			double neutral_pos = calcNeutralPos(assets,currency, curTicker.last);
			double ps = calcPower(curTicker.last, neutral_pos * curTicker.last);
			return new Strategy_Stairs(cfg, State {curTicker.last, 0, curTicker.last, 0, neutral_pos, ps,  0});
		}
	}

}

double Strategy_Stairs::getEquilibrium() const {
	return st.price;
}

PStrategy Strategy_Stairs::reset() const {
	return new Strategy_Stairs(cfg);
}

Strategy_Stairs::Strategy_Stairs(const Config &cfg):cfg(cfg) {
}

Strategy_Stairs::Strategy_Stairs(const Config &cfg, const State &state):cfg(cfg),st(state) {
}

std::string_view Strategy_Stairs::id = "stairs";

std::string_view  Strategy_Stairs::getID() const {
	return id;
}

json::Value Strategy_Stairs::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object
			("Last price", minfo.invert_price?1.0/st.price:st.price)
			("Position", minfo.invert_price?-st.pos:st.pos)
			("Step", minfo.invert_price?-st.step:st.step)
			("Velocity", minfo.invert_price?-st.power:st.power)
			("Neutral position", minfo.invert_price?-st.neutral_pos:st.neutral_pos)
			("Open price", minfo.invert_price?1.0/st.open:st.open);

}

PStrategy Strategy_Stairs::importState(json::Value src) const {
	return new Strategy_Stairs(cfg, State{
		src["price"].getNumber(),
		src["pos"].getNumber(),
		src["open"].getNumber(),
		src["value"].getNumber(),
		src["neutral_pos"].getNumber(),
		src["power"].getNumber(),
		src["step"].getInt()
	});
}
double Strategy_Stairs::assetsToPos(double assets) const {
	return assets - st.neutral_pos;
}
double Strategy_Stairs::posToAssets(double pos) const {
	return pos + st.neutral_pos;
}

double Strategy_Stairs::calcNeutralPos(double assets, double currency, double price) const {
	double value = assets * price + currency;
	double middle = value/2;
	return middle/price;
}

