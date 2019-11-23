/*
 * strategy_plfrompos.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy_plfrompos.h"
#include <cmath>
#include <imtjson/object.h>

#include "../shared/logOutput.h"
#include "sgn.h"

using ondra_shared::logDebug;
using ondra_shared::logInfo;

std::string_view Strategy_PLFromPos::id = "plfrompos";

Strategy_PLFromPos::Strategy_PLFromPos(const Config &cfg, const State &st)
	:cfg(cfg),st(st)
{
}


bool Strategy_PLFromPos::isValid() const {
	return st.inited;
}

double Strategy_PLFromPos::calcK(const State &st) {
	return st.step / (pow2(st.p) * 0.01);
}

double Strategy_PLFromPos::calcK() const {
	return calcK(st);
}

PStrategy Strategy_PLFromPos::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (st.inited && st.valid_power) return this;
	double last = curTicker.last;
	State newst = st;
	if (!st.inited) {
		newst.p = last;
		newst.a = assets;
	}

	newst.inited = true;

	if (!st.valid_power || !cfg.power) {
		calcPower(newst, last, assets, currency);
	}

	if (!st.inited) {
		double pos = assetsToPos(minfo,assets);
		newst.value = pow2(pos) / (2* calcK(newst));
	}


	return new Strategy_PLFromPos(cfg,newst);

}


double Strategy_PLFromPos::calcNewPos(const IStockApi::MarketInfo &minfo, double tradePrice) const {
	double maxpos = st.maxpos?st.maxpos:std::numeric_limits<double>::max();
	//calculate direction of the line defines position change per price change
	double k = calcK();
	double pos = assetsToPos(minfo,st.a);
	double p = st.p;

	//calculate new position on new price
	double np = pos + (p - tradePrice) * k;
	//get absolute value of the position
	double ap = std::abs(np);
	//if new position is reduced, but not reversed
	if (ap < std::abs(pos) && np * pos > 0 && std::abs(pos) <= maxpos) {
		//calculate profit made from moving price from p to tradePrice
		//profit is defined by current position * difference of two prices
		//also increas or decrease it by reduce factor, which can be configured (default 1)
		double s = (pos - np) * (tradePrice - p)*cfg.reduce_factor;
		//calculate inner of sqrt();
		//it expect, that prices moves to tradePrice and makes profit 's'
		//then part of position is closed to value 'np'
		//absolute value of position is 'ap'
		//we need calculate new np where this profit is used to reduce position
		//
		//new position squared is calculated here
		//(check: s = 0 -> ap = ap)
		//(check: s > 0 -> ap goes down
		double np2 = ap*ap - 2 * k * s;
		//if result is non-negative
		if (np2 > 0) {
			//calculate new positon by sqrt(np2) and adding signature
			np = sgn(np) * sqrt(np2);
		} //otherwise stick with original np
		ap = std::abs(np);
	}
	//adjust np, if max position has been reached
	if (ap > maxpos) {
		np = sgn(np)*(ap + maxpos)/2;
	}
	return posToAssets(minfo,np);
}

std::pair<Strategy_PLFromPos::OnTradeResult, PStrategy> Strategy_PLFromPos::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	PStrategy s;
	if (st.inited) {
		s = this;
	} else {
		s = onIdle(minfo, IStockApi::Ticker{tradePrice,tradePrice,tradePrice}, assetsLeft, currencyLeft);
	}
	return static_cast<const Strategy_PLFromPos &>(*s).onTrade2(minfo,tradePrice, tradeSize,assetsLeft, currencyLeft);

}


std::pair<Strategy_PLFromPos::OnTradeResult, PStrategy> Strategy_PLFromPos::onTrade2(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	State newst = st;

	double k = calcK();
	double act_pos = assetsToPos(minfo,assetsLeft);
	double prev_pos = act_pos - tradeSize;
	double new_pos = tradeSize?calcNewPos(minfo,tradePrice):prev_pos;
	//realised profit/loss
	double rpl = prev_pos * (tradePrice - st.p);
	//position potential value (pos^2 / (2*k) - surface of a triangle)
	double posVal = act_pos*act_pos/(2*k);
	//unrealised profit/loss change
	double upl = posVal - st.value;
	//potential change
	double ef = rpl + upl;
	//normalized profit
	double np = ef * (1 - cfg.accum);
	//normalized accumulated
	double ap = (ef * cfg.accum)/tradePrice;

	newst.acm  = st.acm + ap;
	newst.p = tradePrice;
	newst.a = new_pos;
	newst.value = posVal;

	calcPower(newst, tradePrice, assetsLeft, currencyLeft);
	return {
		OnTradeResult{np,ap},
		new Strategy_PLFromPos(cfg,newst)
	};
}

json::Value Strategy_PLFromPos::exportState() const {
	if (st.inited)
	return json::Object
			("p",st.p)
			("a",st.a)
			("acm",st.acm)
			("maxpos",st.maxpos)
			("step",st.step)
			("val", st.value)
			("pw", (int)(cfg.power*1000));
	else return json::undefined;
}

PStrategy Strategy_PLFromPos::importState(json::Value src) const {
	if (src.hasValue()) {
		State newst {
			src["a"].defined(),
			true,
			src["p"].getNumber(),
			src["a"].getNumber(),
			src["step"].getNumber(),
			src["maxpos"].getNumber(),
			src["acm"].getNumber(),
			src["val"].getNumber(),
		};
		int curpw = (int)(cfg.power*1000);
		if (curpw != src["pw"].getUInt()) {
			newst.valid_power = false;
		}
		return new Strategy_PLFromPos(cfg, newst);
	} else {
		return this;
	}
}

Strategy_PLFromPos::OrderData Strategy_PLFromPos::getNewOrder(
		const IStockApi::MarketInfo &minfo,
		double price, double dir, double assets, double currency) const {
	double new_pos = calcNewPos(minfo, price);
	double sz = calcOrderSize(st.a, assets, new_pos);
	return OrderData {0, sz};
}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	double pos = assetsToPos(minfo,assets);
	double p = st.p;
	double k = calcK();
	double mp = pos / k + p;
	if (cfg.maxpos) {
		return MinMax {
			-cfg.maxpos /k + mp,
			cfg.maxpos /k + mp
		};
	} else {
		return MinMax {
			p - sqrt(2*currencies/calcK()),
			assets / calcK() + p
		};
	}
}

double Strategy_PLFromPos::getEquilibrium() const {
	return st.p;
}

std::string_view Strategy_PLFromPos::getID() const {
	return id;
}

PStrategy Strategy_PLFromPos::reset() const {
	return new Strategy_PLFromPos(cfg,{});
}


double Strategy_PLFromPos::getNeutralPos(
		const IStockApi::MarketInfo &minfo) const {
	return (minfo.invert_price?-1.0:1.0)*cfg.neutral_pos+st.acm;
}

double Strategy_PLFromPos::assetsToPos(const IStockApi::MarketInfo &minfo,
	double assets) const {
	return assets - getNeutralPos(minfo);
}

double Strategy_PLFromPos::posToAssets(const IStockApi::MarketInfo &minfo,
	double pos) const {
	return pos + getNeutralPos(minfo);
}

void Strategy_PLFromPos::calcPower(State& st, double price, double , double currency) const
{
	if (cfg.power) {
		double step = currency * std::pow(10, cfg.power)*0.01;
		double k = step / (price * price * 0.01);
		double max_pos = sqrt(k * currency);
		st.maxpos = max_pos;
		st.step = step;
		logInfo("Strategy recalculated: step=$1, max_pos=$2", step, max_pos);
	} else {
		st.maxpos = cfg.maxpos;
		st.step = cfg.step;
	}
	st.valid_power = true;
}

