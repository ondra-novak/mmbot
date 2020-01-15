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

double Strategy_PLFromPos::reducedK(double k) const {
	if (cfg.reduceMode == fixedReduce) k = k * (1+cfg.reduce_factor);
	return k;
}




PStrategy Strategy_PLFromPos::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (st.inited && st.valid_power && std::isfinite(st.a) && std::isfinite(st.p) && std::isfinite(st.step) && std::isfinite(st.acm) && std::isfinite(st.value) && st.step) return this;
	double last = curTicker.last;
	State newst = st;
	if (!st.inited || !std::isfinite(st.p) || !std::isfinite(st.a)) {
		newst.p = last;
		newst.a = assets;
		calcPower(newst, last, assets, currency);
	}

	if (!std::isfinite(st.value)) newst.value = 0;
	if (!std::isfinite(st.acm)) newst.acm = 0;

	newst.inited = true;

	if (!st.valid_power || !cfg.power || st.step == 0) {
		calcPower(newst, last, assets, currency);
	}

	if (!st.inited) {
		double pos = assetsToPos(minfo,assets);
		newst.value = std::abs(pos)>minfo.asset_step/10?(pow2(pos) / (2* reducedK(calcK(newst)))):0;
	}


	return new Strategy_PLFromPos(cfg,newst);

}


double Strategy_PLFromPos::calcNewPos(const IStockApi::MarketInfo &minfo, double tradePrice) const {
	double maxpos = st.maxpos?st.maxpos:std::numeric_limits<double>::max();
	//calculate direction of the line defines position change per price change
	double k = calcK();
	double pos = assetsToPos(minfo,st.a);
	if (k == 0) return pos;

	double p = st.p;
	double np = pos;

	//requested trade dir: 1=buy, -1=sell;
	double dir = sgn(p - tradePrice);
	//new position should be reduced (-1 * 1 or 1 * -1 = decreaseing)
	bool dcrs = pos * dir <= 0;

	//if position is increasing at maxpos do not increase position beyond that limit
	if (dcrs || std::abs(pos) <= maxpos) {
		//so position is decreasing or we did not reach maxpos

		if (cfg.reduceMode == neutralMove) {
			//neutralMove reduction, calculate neutral_price.
			//the formula is inverse to k * (neutral_price - p) = pos
			double neutral_price = pos/k + p;
			//adjust reduce factor, it is in per milles
			double c = cfg.reduce_factor * 0.1;
			//calculate reverse reduction of reduce factor, if position is decreasing (or 1)
			double r = (dcrs?-0.9:1.0);
			//calculate new neutral position as move position of amount of distance to tradePrice * reduce factor * adjustment
			double new_neutral_price = neutral_price+(tradePrice-neutral_price)*c*r;
			//calculate new position using new neutral price and requested tradePrice with same k
			np = k * (new_neutral_price - tradePrice);

		} else {


			//calculate new position on new price
			np = pos + (p - tradePrice) * k;
			//get absolute value of the position
			double ap = std::abs(np);


			if (dcrs  && np * pos > 0) {
				double reduce_factor = cfg.reduce_factor;
				//calculate profit made from moving price from p to tradePrice
				//profit is defined by current position * difference of two prices
				//also increas or decrease it by reduce factor, which can be configured (default 1)
				double s = (pos - np) * (tradePrice - p)*reduce_factor;
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
					double nnp;
					//depend of type of reduction
					switch (cfg.reduceMode) {
					default:
						//fixed reduction is easy
					case fixedReduce: 	nnp = pos + (np - pos) * (1 + reduce_factor); break;
						//result from first part is extra reduction powered by 2. Now sqare root of it
					case reduceFromProfit: nnp = sgn(np) * sqrt(np2);break;
					}
					np = nnp;
				} //otherwise stick with original np
			}
		}
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
	double new_pos = calcNewPos(minfo,tradePrice);
	//realised profit/loss
	double rpl = prev_pos * (tradePrice - st.p);
	//position potential value (pos^2 / (2*k) - surface of a triangle)
	double posVal = act_pos*act_pos/(2*reducedK(k));
	//unrealised profit/loss change
	double upl = posVal - st.value;
	//potential change
	double ef = rpl + upl;
	//normalized profit
	double np = ef * (1 - cfg.accum);
	//normalized accumulated
	double ap = (ef * cfg.accum)/tradePrice;
	double p0 = new_pos/reducedK(k) + tradePrice;

	newst.acm  = st.acm + ap;
	newst.p = tradePrice;
	newst.a = new_pos;
	newst.value = posVal;

	double fpos = assetsToPos(minfo, new_pos);
	double ppos = assetsToPos(minfo,st.a);

	if (std::abs(fpos) <= cfg.maxpos*1e-10 || std::abs(fpos) > std::abs(ppos))
			calcPower(newst, tradePrice, assetsLeft, currencyLeft);
	return {
		OnTradeResult{np,ap,p0},
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
			("pw", (int)(cfg.power*100000)+(int)(cfg.baltouse*100));
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
		int curpw = (int)(cfg.power*100000)+(int)(cfg.baltouse*100);
		if (curpw != (int)src["pw"].getInt()) {
			newst.valid_power = false;
		}
		return new Strategy_PLFromPos(cfg, newst);
	} else {
		return this;
	}
}

Strategy_PLFromPos::OrderData Strategy_PLFromPos::getNewOrder(
		const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets, double currency) const {
	double pos = assetsToPos(minfo, st.a);
	bool atmaxpos = cfg.maxpos && (pos < -cfg.maxpos || pos > cfg.maxpos);
	if (atmaxpos) {
		float f = pow2(cfg.maxpos/pos)*0.5;
		new_price = st.p * (1-f) + new_price * f;
	}
	double new_pos = calcNewPos(minfo, new_price);
	double sz = calcOrderSize(st.a, assets, new_pos);
	double minsz = std::max(minfo.min_size*1.2, (minfo.min_volume/cur_price)*1.2);
	if (dir*sz < minsz) sz = dir * minsz;
	return OrderData {new_price, sz};
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
		double c = currency * cfg.baltouse;
		double step = c * std::pow(10, cfg.power)*0.01;
		double k = step / (price * price * 0.01);
		double max_pos = sqrt(k * c);
		st.maxpos = max_pos;
		st.step = step;
		logInfo("Strategy recalculated: step=$1, max_pos=$2", step, max_pos);
	} else {
		st.maxpos = cfg.maxpos;
		st.step = cfg.step;
	}
	st.valid_power = true;
}

json::Value Strategy_PLFromPos::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	return json::Object
			("Last price",minfo.invert_price?1.0/st.p:st.p)
			("Position",assetsToPos(minfo, st.a)*(minfo.invert_price?-1.0:1.0))
			("Accumulated",st.acm?json::Value(st.acm):json::Value())
			("Max allowed position",st.maxpos)
			("Step",st.step)
			("Value of the position", st.value);

}
