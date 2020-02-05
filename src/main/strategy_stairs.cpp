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
	auto idir = static_cast<int>(dir);
	if (idir * cs >= 0) {
		cs = cs + idir;
	} else {
		switch (cfg.reduction) {
		case step1: cs = cs + idir;break;
		case step2: cs = cs + 2*idir;break;
		case step3: cs = cs + 3*idir;break;
		case step4: cs = cs + 4*idir;break;
		case step5: cs = cs + 5*idir;break;
		case half: cs = cs + cfg.max_steps*idir/2;break;
		case close: cs = 0;break;
		case reverse: cs = idir;break;
		}
	}
	return std::min(std::max(cs, -cfg.max_steps), cfg.max_steps);
}

template<typename Fn>
void Strategy_Stairs::serie(Pattern pat, Fn &&fn) {
	int idx;
	double sum;
	switch(pat) {
	case constant: idx = 0;
		while (fn(idx,idx)) idx++;
		break;
	case harmonic: sum = 0; idx=0;
		while (fn(idx,sum)) {++idx; sum = sum + 1.0/idx;}
		break;
	case arithmetic: sum = 0; idx=0;
		while (fn(idx,sum)) {++idx; sum = sum + idx;}
		break;
	case exponencial:
		if (fn(0, 0)) {
			sum = 1;
			idx = 1;
			while (fn(idx,sum)) {sum = sum + sum;++idx;}
		}
		break;
	}
}

double Strategy_Stairs::stepToPos(std::intptr_t step) const {
	if (cfg.pattern == constant) return step;
	else {
		double mlt = sgn(step);
		std::intptr_t istep = std::abs(step);
		double res = 0;
		serie(cfg.pattern, [&](int idx, double amount){res = amount; return idx < istep;});
		return res*mlt;
	}
}
std::intptr_t Strategy_Stairs::posToStep(double pos) const{
	std::intptr_t s = sgn(pos);
	double p = std::abs(pos);
	if (cfg.pattern == constant) return static_cast<std::intptr_t>(std::round(p)) * s;
	else {
		std::intptr_t r = 0;
		serie(cfg.pattern, [&](int idx, double amount){r = idx; return amount<p && idx < cfg.max_steps;});
		return r * s;
	}
}



IStrategy::OrderData Strategy_Stairs::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency) const {

	double power = st.power;
	auto step = getNextStep(dir);
	double new_pos = power * stepToPos(step);
	return OrderData{0.0,calcOrderSize(posToAssets(st.pos),assets, posToAssets(	new_pos))};
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > Strategy_Stairs::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	if (!isValid()) return onIdle(minfo,{tradePrice,tradePrice,tradePrice,0},assetsLeft,currencyLeft)
						->onTrade(minfo,tradePrice,tradeSize,assetsLeft,currencyLeft);
//	double dir = sgn(tradeSize);
	double power = st.power;
	double curpos = assetsToPos(assetsLeft);
	double prevpos = curpos - tradeSize;
	intptr_t step = posToStep(std::round(curpos/power));
	State nst = st;
	nst.price = tradePrice;
	nst.pos = curpos;
	nst.open = curpos*prevpos <=0 ? tradePrice:(st.open*prevpos + tradeSize*tradePrice)/curpos;
	nst.step = step;
	double spread = std::abs(tradePrice - st.price);
	double astp = std::abs(nst.step);
	nst.value = power * spread * astp* (astp+1) / 2;
	if (nst.pos * st.pos <=0) {
		if (minfo.leverage) {
			nst.power = calcPower(tradePrice, currencyLeft);
		} else {
			nst.neutral_pos = calcNeutralPos(assetsLeft,currencyLeft, tradePrice);
			nst.power = calcPower(tradePrice, nst.neutral_pos * tradePrice);
		}
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
	double steps = stepToPos(cfg.max_steps);
	return (currency / price * std::pow(10, cfg.power)*0.01)/steps;
}

PStrategy Strategy_Stairs::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		PStrategy g;
		if (minfo.leverage) {
			double ps = calcPower(curTicker.last, currency);
			g = new Strategy_Stairs(cfg, State {curTicker.last, 0, curTicker.last, 0, 0, ps,  0});
		} else {
			double neutral_pos = calcNeutralPos(assets,currency, curTicker.last);
			double ps = calcPower(curTicker.last, neutral_pos * curTicker.last);
			g = new Strategy_Stairs(cfg, State {curTicker.last, 0, curTicker.last, 0, neutral_pos, ps,  0});
		}
		if (g->isValid()) return g;
		else throw std::runtime_error("Stairs: Invalid settings - unable to initialize strategy");
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

