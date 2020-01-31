/*
 * strategy_plfrompos.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy_plfrompos.h"
#include <cmath>
#include <imtjson/object.h>
#include <imtjson/string.h>

#include "../shared/logOutput.h"
#include "sgn.h"

using ondra_shared::logDebug;
using ondra_shared::logInfo;

double Strategy_PLFromPos::sliding_zero_factor = 0.95;
double Strategy_PLFromPos::min_rp_reduce = 0.1;

std::string_view Strategy_PLFromPos::id = "plfrompos";

Strategy_PLFromPos::Strategy_PLFromPos(const Config &cfg, const State &st)
	:cfg(cfg),st(st)
{
}


bool Strategy_PLFromPos::isValid() const {
	return st.inited;
}

double Strategy_PLFromPos::calcK(const State &st) {
	return st.k;
}

double Strategy_PLFromPos::calcK() const {
	return calcK(st);
}

double Strategy_PLFromPos::reducedK(double k) const {
/*	if (cfg.reduceMode == fixedReduce) k = k * (1+cfg.reduce_factor);*/
	return k;
}




PStrategy Strategy_PLFromPos::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (st.inited && st.valid_power && st.k && std::isfinite(st.a) && std::isfinite(st.p) && std::isfinite(st.k) && std::isfinite(st.acm) && std::isfinite(st.value) ) return this;
	double last = curTicker.last;
	State newst = st;
	if (!st.inited || !std::isfinite(st.p) || !std::isfinite(st.a)) {
		newst.p = last;
		newst.a = assets;
		logDebug("not inited");
		calcPower(minfo, newst, last, assets, currency,true);
	}

	if (!std::isfinite(st.value)) newst.value = 0;
	if (!std::isfinite(st.acm)) newst.acm = 0;

	newst.inited = true;

	if (!st.valid_power || !cfg.power || st.k== 0) {
		logDebug("not valid power");
		calcPower(minfo, newst, last, assets, currency, true);
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
	bool atmax = std::abs(pos) > maxpos;

	double p = st.p;
	double np = pos;

	//requested trade dir: 1=buy, -1=sell;
	double dir = sgn(p - tradePrice);
	//new position should be reduced (-1 * 1 or 1 * -1 = decreaseing)
	bool dcrs = pos * dir < 0;


	//if position is increasing at maxpos do not increase position beyond that limit
	if (!atmax || dcrs)
	{
		//so position is decreasing or we did not reach maxpos

		if (cfg.reduceMode == neutralMove) {
				//neutralMove reduction, calculate neutral_price.
				//the formula is inverse to k * (neutral_price - p) = pos
				double neutral_price = pos/k + p;
				//adjust reduce factor, it is in per milles
				double c = cfg.reduce_factor * 0.1;
				//calculate reverse reduction of reduce factor, if position is decreasing (or 1)
				double r = (dcrs?-sliding_zero_factor:1.0);
				//calculate new neutral position as move position of amount of distance to p * reduce factor * adjustment
				double new_neutral_price = neutral_price+(p-neutral_price)*c*r;
				//calculate new position using new neutral price and requested tradePrice with same k
				np = k * (new_neutral_price - tradePrice);
				if (dcrs && std::abs(np) > std::abs(pos) && np * pos > 0)
					np = pos;



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
					case reduceFromProfit: {
						nnp = sgn(np) * sqrt(np2);
						double nnp2 = pos + (np - pos) * (1 + min_rp_reduce*reduce_factor);
						if (std::abs(nnp) > std::abs(nnp2)) nnp = nnp2;;
						break;
					}


					case toOpenPrice: {
						double openPrice = st.avgsum/std::abs(pos);
						double f = (tradePrice - st.p)/(openPrice - st.p);
						if (std::isfinite(f) && f >= 0 && f <= 1) {
							nnp = (pos * (1-f))*reduce_factor + np * (1-reduce_factor);
						} else {
							nnp = np;
						}
						} break;
					case ema: {
						double neutral_price = pos/k + p;
						double z = 2/(cfg.reduce_factor*1000+1);
						double neutral_price_ema = p*z + neutral_price*(1-z);
						nnp = k * (neutral_price_ema - tradePrice);
					} break;
					case overload: {
						nnp = ((p - pos/k - tradePrice) * k)*0.99;
					}break;
					}
					np = nnp;
				} //otherwise stick with original np
			}
		}
	}
	return np;
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
	newst.a = posToAssets(minfo,new_pos);
	newst.value = posVal;

	double ppos = assetsToPos(minfo,st.a);
	double afpos = std::abs(new_pos);
	double appos = std::abs(ppos);
	bool reversal =  new_pos * ppos <= 0;

	newst.avgsum = ( reversal || st.avgsum == 0)?(afpos * tradePrice):(st.avgsum + (afpos - appos) * tradePrice );
	if (st.maxpos) {
		if (st.maxpos < afpos) {
			newst.mult = 0.05;
		}
	 else {
		newst.mult = (st.mult * 2);
		if (newst.mult > 1.0) newst.mult =1.0;
	}
	}

	if (cfg.reduceMode == overload) {
		newst.mult = tradeSize?1:0;
	}

	if (reversal) {
		logDebug("Position reversal: new_pos=$1, ppos=$2", new_pos, ppos);
		calcPower(minfo, newst, newst.p, newst.a, currencyLeft,true);
	}
	return {
		OnTradeResult{np,ap,p0, afpos?newst.avgsum/afpos:newst.p},
		new Strategy_PLFromPos(cfg,newst)
	};
}

json::Value Strategy_PLFromPos::cfgStateHash() const {
	json::Value v( {cfg.power,cfg.baltouse,cfg.step});
	std::string txt = v.toString().str();
	std::size_t h = std::hash<std::string>()(txt);
	logDebug("Strategy settings hash: $1", h);
	return std::to_string(h);

}

json::Value Strategy_PLFromPos::exportState() const {
	if (st.inited)
	return json::Object
			("p",st.p)
			("a",st.a)
			("k",st.k)
			("avg",st.avgsum)
			("acm",st.acm)
			("maxpos",st.maxpos)
			("val", st.value)
			("mult", st.mult)
			("cfg",cfgStateHash());
	else return json::undefined;
}

PStrategy Strategy_PLFromPos::importState(json::Value src) const {
	if (src.hasValue()) {
		State newst {
			src["a"].defined(),
			true,
			src["p"].getNumber(),
			src["a"].getNumber(),
			src["k"].getNumber(),
			src["maxpos"].getNumber(),
			src["acm"].getNumber(),
			src["val"].getNumber(),
			src["avg"].getNumber(),
			src["mult"].getValueOrDefault(1.0),
		};
		json::Value chash = src["cfg"];
		newst.valid_power = chash == cfgStateHash();
		return new Strategy_PLFromPos(cfg, newst);
	} else {
		return this;
	}
}

Strategy_PLFromPos::OrderData Strategy_PLFromPos::getNewOrder(
		const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets, double currency) const {
	double pos = assetsToPos(minfo, st.a);
	double act_pos = assetsToPos(minfo, assets);
	bool atmaxpos = st.maxpos && std::abs(pos) > st.maxpos;
	double half_price = (new_price + st.p) * 0.5;
	if (atmaxpos) {
		double zeroPos = posToAssets(minfo, -sgn(pos)*cfg.stoploss_reverse*st.maxpos);
		double osz = calcOrderSize(assets,assets,zeroPos);
		if (dir * pos < 0 && act_pos * dir < 0) {
			return OrderData{cur_price, osz, true};
		} else {
			return OrderData { half_price, osz, true };
		}
	}
	double new_pos = calcNewPos(minfo, new_price);
	if (st.maxpos && std::abs(new_pos) > st.maxpos) {
		double new_pos2 = calcNewPos(minfo, half_price);
		if (st.maxpos && std::abs(new_pos2) > st.maxpos) {
			return OrderData{half_price,0,true};
		} else{
			return OrderData{0,0,true};
		}
	}
	else return OrderData {0, calcOrderSize(st.a, assets, posToAssets(minfo,new_pos))*st.mult};
}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	double pos = assetsToPos(minfo,assets);
	double p = st.p;
	double k = calcK();
	double mp = pos / k + p;
	if (st.maxpos) {
		return MinMax {
			-st.maxpos /k + mp,
			st.maxpos /k + mp
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

void Strategy_PLFromPos::calcPower(const IStockApi::MarketInfo &minfo, State& st, double price, double assets, double currency, bool keepnp) const
{
	double pos = assetsToPos(minfo, assets);
	double new_pos = pos;
//	double neutral_price = pos/calcK(st) + price;
//	double ref_price = std::isfinite(neutral_price)?neutral_price:price;
	if (cfg.power) {
		double c = currency * cfg.baltouse;
		double step = c * std::pow(10, cfg.power)*0.01;
		double k = step / (price * price* 0.01);
		double max_pos = sqrt(k * c);
		st.maxpos = max_pos;
		st.k= k;
		logInfo("Strategy recalculated: step=$1, pos=$3, max_pos=$2, new_pos=$4", step, max_pos, pos, new_pos);
	} else {
		st.maxpos = cfg.maxpos;
		st.k= cfg.step / (price * price* 0.01);
	}
/*	if (keepnp && std::isfinite(neutral_price)) {
		logDebug("Position recalculated: $1 -> $2", pos, new_pos);
		new_pos = st.k * (neutral_price - price);
		st.a = posToAssets(minfo, new_pos);
	}*/
	st.valid_power = true;
}

json::Value Strategy_PLFromPos::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	double pos = assetsToPos(minfo, st.a);
	double openPrice = st.avgsum/std::abs(pos);
	return json::Object
			("Last price",minfo.invert_price?1.0/st.p:st.p)
			("Position",assetsToPos(minfo, st.a)*(minfo.invert_price?-1.0:1.0))
			("Accumulated",st.acm?json::Value(st.acm):json::Value())
			("Max allowed position",st.maxpos)
			("Step",st.k*st.p*st.p*0.01)
			("Open price", minfo.invert_price?1.0/openPrice:openPrice)
			("Value of the position", st.value);

}
