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


PStrategy Strategy_PLFromPos::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (st.inited && st.valid_power && st.k && std::isfinite(st.a) && std::isfinite(st.p) && std::isfinite(st.k) && std::isfinite(st.value) ) return this;
	double last = curTicker.last;
	State newst = st;
	if (!st.inited || !std::isfinite(st.p) || !std::isfinite(st.a) || !st.k) {
		newst.p = last;
		newst.a = assets;
		logDebug("not inited");
		calcPower(minfo, newst, last, assets, currency);
	}

	newst.value = pow2(assets-newst.neutral_pos)/(2*newst.k);

	newst.inited = true;

	if (!st.valid_power || !cfg.power || st.k== 0) {
		logDebug("not valid power");
		calcPower(minfo, newst, last, assets, currency);
	}


	return new Strategy_PLFromPos(cfg,newst);

}


double Strategy_PLFromPos::calcNewPos(const IStockApi::MarketInfo &minfo, double tradePrice) const {
	//double maxpos = st.maxpos?st.maxpos:std::numeric_limits<double>::max();
	//calculate direction of the line defines position change per price change
	double k = calcK();
	double pos = assetsToPos(st.a);
	if (k == 0) return pos;

	//bool atmax = apos > maxpos;

	double p = st.p;
	double np = pos;

	//requested trade dir: 1=buy, -1=sell;
	double dir = sgn(p - tradePrice);
	//new position should be reduced (-1 * 1 or 1 * -1 = decreaseing)
	bool dcrs = pos * dir < 0;

	double reduce_factor = cfg.reduce_factor;


	//if position is increasing at maxpos do not increase position beyond that limit
	//if (!atmax || dcrs)
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


			if ((dcrs || cfg.reduce_on_increase)  && np * pos > 0) {
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
					case fixedReduce: 	nnp = pos + (np - pos) * (dcrs?(1 + reduce_factor):1/(1 + reduce_factor)); break;
						//result from first part is extra reduction powered by 2. Now sqare root of it
					case reduceFromProfit: nnp = sgn(np) * sqrt(np2);break;

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
	return static_cast<const Strategy_PLFromPos &>(*s).onTrade2(minfo,tradePrice, tradeSize,assetsLeft, currencyLeft, false);

}

bool Strategy_PLFromPos::atMax(const IStockApi::MarketInfo &minfo, const State &st) const {
	return st.maxpos && !isExchange(minfo) && std::abs(st.a - st.neutral_pos) > st.maxpos;
}

std::pair<Strategy_PLFromPos::OnTradeResult, PStrategy> Strategy_PLFromPos::onTrade2(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft, bool sub) const {


	State newst = st;

	double k = calcK();
	double act_pos = assetsToPos(assetsLeft);
	double prev_pos = act_pos - tradeSize;
	double new_pos = calcNewPos(minfo,tradePrice);


	if (st.suspended) {
		auto sub = st.suspended->onTrade2(minfo, tradePrice, 0, assetsLeft, currencyLeft, true);
		auto substr = PMyStrategy::staticCast(sub.second);
		if (!substr->atMax(minfo, substr->st)) {
			return sub;
		}
		else {
			newst.suspended = substr;
		}
	}



	newst.p = tradePrice;
	newst.a = posToAssets(new_pos);

	double ppos = assetsToPos(st.a);
	double afpos = std::abs(new_pos);
	double appos = std::abs(ppos);
	bool reversal =  new_pos * ppos <= 0;

	newst.avgsum = ( reversal || st.avgsum == 0)?(afpos * tradePrice):(st.avgsum + (afpos - appos) * tradePrice );

	bool atmax = atMax(minfo,st);
	if (!atmax) {
		calcPower(minfo, newst, tradePrice, assetsLeft, currencyLeft);
	}

	//realised profit/loss
	double rpl = prev_pos * (tradePrice - st.p);
	//position potential value (pos^2 / (2*k) - surface of a triangle)
	double posVal = pow2(assetsLeft - newst.neutral_pos)/(2*newst.k);
	//unrealised profit/loss change
	double upl = posVal - st.value;
	//potential change
	double ef = rpl + upl;
	//normalized profit
	double np = ef;
	//normalized accumulated
	double ap = 0;
	double p0 = new_pos/k + tradePrice;

	newst.value = posVal;
	PMyStrategy newinst = new Strategy_PLFromPos(cfg,newst);

	if (atmax && tradeSize == 0 && !sub) {
		State nst2;
		nst2.suspended = newinst;
		nst2.inited = false;
		newinst = new Strategy_PLFromPos(halfConfig(cfg), nst2);
	}

	return {OnTradeResult{np,ap,p0, afpos?newst.avgsum/afpos:newst.p}, PStrategy::staticCast(newinst)};
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
			("maxpos",st.maxpos)
			("val", st.value)
			("neutral_pos", st.neutral_pos)
			("suspended", st.suspended != nullptr?st.suspended->exportState():json::Value())
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
			src["val"].getNumber(),
			src["avg"].getNumber(),
			src["neutral_pos"].getNumber(),
			PMyStrategy::staticCast(src["suspended"].hasValue()?importState(src["suspended"]):nullptr)
		};
		json::Value chash = src["cfg"];
		newst.valid_power = chash == cfgStateHash();
		Config ncfg(newst.suspended != nullptr?halfConfig(newst.suspended->cfg):cfg);
		return new Strategy_PLFromPos(ncfg, newst);
	} else {
		return this;
	}
}

Strategy_PLFromPos::OrderData Strategy_PLFromPos::getNewOrder(
		const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets, double currency) const {
	double pos = assetsToPos(st.a);
	double act_pos = assetsToPos(assets);
	if (isExchange(minfo)) {
		double new_pos = calcNewPos(minfo, new_price);
		double sz = calcOrderSize(st.a, assets, posToAssets(new_pos));
		double szf = sz, prf = new_price;
		minfo.addFees(szf, prf);
		if (szf + assets <= 0 || currency-szf*prf <= 0) {
//			logDebug("Order skipped: $1<=0 or $2 <=0, szf=$3, prf=$4, size=$5, price=$6", szf + assets, currency-szf*prf, szf, prf, sz, new_price);
			return OrderData{0, 0, Alert::forced};
		} else {
			return OrderData{0, sz};
		}
	} else {
		bool atmaxpos = st.maxpos && std::abs(pos) > st.maxpos;
		if (atmaxpos) {
			double zeroPos = posToAssets(0);
			double osz = calcOrderSize(assets,assets,zeroPos);
			if (dir * pos < 0 && act_pos * dir < 0) {
				return OrderData{cur_price, osz, Alert::stoploss};
			} else {
				return OrderData { 0, osz, Alert::stoploss};
			}
		}
		double new_pos = calcNewPos(minfo, new_price);
		if (st.maxpos && std::abs(new_pos) > st.maxpos) {
			return OrderData{0,0,Alert::forced};
		}
		else return OrderData {0, calcOrderSize(st.a, assets, posToAssets(new_pos))};
	}
}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(
		const IStockApi::MarketInfo &minfo,
		double assets,
		double currencies) const {

	double pos = assetsToPos(assets);
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

bool Strategy_PLFromPos::isAuto() const {
	return cfg.power != 0;
}

double Strategy_PLFromPos::assetsToPos(double assets) const {
	return assets - st.neutral_pos;
}

double Strategy_PLFromPos::posToAssets(double pos) const {
	return pos + st.neutral_pos;
}

void Strategy_PLFromPos::calcPower(const IStockApi::MarketInfo &minfo, State& st, double price, double assets, double currency) const
{
	auto wp = calcNeutralBalance(minfo, assets, currency, price);
	double pos = st.a - wp.neutral_pos;
	if (cfg.power) {
		double value = pos*pos/(2*calcK(st));
		if (!std::isfinite(value)) value = 0;
		double c = wp.balance * cfg.baltouse + value;
		double step = c * std::pow(10, cfg.power)*0.01;
		double k = step / (price * price* 0.01);
		double max_pos = sqrt(k * c);
		st.maxpos = max_pos;
		st.k= k;
	} else {
		st.maxpos = cfg.maxpos;
		st.k= cfg.step / (price * price* 0.01);
	}
	st.neutral_pos = wp.neutral_pos;
	st.valid_power = true;
}

json::Value Strategy_PLFromPos::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	double pos = assetsToPos(st.a);
	double openPrice = st.avgsum/std::abs(pos);
	return json::Object
			("Last price",minfo.invert_price?1.0/st.p:st.p)
			("Position",assetsToPos(st.a)*(minfo.invert_price?-1.0:1.0))
			("Neutral position",minfo.invert_price?-st.neutral_pos:st.neutral_pos)
			("Max allowed position",st.maxpos)
			("Step",st.k*st.p*st.p*0.01)
			("Open price", minfo.invert_price?1.0/openPrice:openPrice)
			("Value of the position", st.value);

}

bool Strategy_PLFromPos::isExchange(const IStockApi::MarketInfo &minfo) const {
	return !minfo.leverage;
}

Strategy_PLFromPos::CalcNeutralBalanceResult Strategy_PLFromPos::calcNeutralBalance(const IStockApi::MarketInfo &minfo, double assets, double currency, double price) const {
	double offset = (minfo.invert_price?-1:1)*cfg.pos_offset;
	if (minfo.leverage) return {offset, currency};
	else {
		double total_balance = assets*price + currency;
		double np = total_balance/(2*price)+offset;
		double balance = total_balance - np*price;
		return {np, balance};
	}
}

Strategy_PLFromPos::Config Strategy_PLFromPos::halfConfig(const Config &cfg) {
	Config c(cfg);
	c.maxpos*=0.5;
	c.baltouse*=0.5;
	c.step*=0.5;
	return c;
}
