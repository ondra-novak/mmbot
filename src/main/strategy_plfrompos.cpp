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

std::string_view Strategy_PLFromPos::id = "plfrompos";

Strategy_PLFromPos::Strategy_PLFromPos(const Config &cfg, double p, double pos, double acm)
	:cfg(cfg),p(p),pos(pos), acm(acm)
{
}


bool Strategy_PLFromPos::isValid() const {
	return p > 0;
}

IStrategy* Strategy_PLFromPos::init(double curPrice, double assets, double ) const {
	return new Strategy_PLFromPos(cfg, curPrice, assets - cfg.neutral_pos);
}



double Strategy_PLFromPos::calcK() const {
	return cfg.step / (pow2(p) * 0.01);
}

double Strategy_PLFromPos::calcNewPos(double tradePrice, bool reducepos) const {
	//calculate direction of the line defines position change per price change
	double k = calcK();
	//calculate new position on new price
	double np = pos + (p - tradePrice) * k;
	//get absolute value of the position
	double ap = std::abs(np);
	//if new position is reduced, but not reversed
	if (ap < std::abs(pos) && np * pos > 0) {
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
	if (cfg.maxpos && ap > cfg.maxpos) {
		return sgn(np)*(ap + cfg.maxpos)/2;
	} else {
		return np;
	}
}

std::pair<Strategy_PLFromPos::OnTradeResult, IStrategy*> Strategy_PLFromPos::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	double k = calcK();
	double act_pos = assetsLeft-acm-cfg.neutral_pos;
	double prev_pos = act_pos - tradeSize;
	double new_pos = tradeSize?calcNewPos(tradePrice,true):prev_pos;
	//realised profit/loss
	double rpl = prev_pos * (tradePrice - p);
	//unrealised profit/loss change
	double upl = 0.5*(act_pos - prev_pos)*(act_pos + prev_pos)/k;
	//potential change
	double ef = rpl + upl;
	//normalized profit
	double np = ef * (1 - cfg.accum);
	//normalized accumulated
	double ap = (ef * cfg.accum)/tradePrice;
	return {
		OnTradeResult{np,ap},
		new Strategy_PLFromPos(cfg,tradePrice, new_pos, acm+ap)
	};
}

json::Value Strategy_PLFromPos::exportState() const {
	return json::Object
			("p",p)
			("pos",pos)
			("acm",acm)
			("np", cfg.neutral_pos);

}

IStrategy* Strategy_PLFromPos::importState(json::Value src) const {
	double new_p = src["p"].getNumber();
	double new_pos = src["pos"].getNumber();
	double new_acm =  src["acm"].getNumber();
	double old_np = src["np"].getNumber();
	new_pos -= (cfg.neutral_pos - old_np);
	if (fabs(old_np -cfg.neutral_pos) > (fabs(old_np)+fabs(cfg.neutral_pos))*0.00001) {
		new_pos += new_acm;
		new_acm = 0;
	}
	return new Strategy_PLFromPos(cfg, new_p, new_pos, new_acm);
}

double Strategy_PLFromPos::getOrderSize(double price, double assets) const {
	bool reducepos = cfg.maxpos == 0 || std::fabs(assets-acm-cfg.neutral_pos) < cfg.maxpos;
	double new_pos = calcNewPos(price, reducepos);
	return calcOrderSize(pos + cfg.neutral_pos + acm, assets, new_pos + cfg.neutral_pos+acm);
}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(double assets,
		double currencies) const {
	double pos = assets-cfg.neutral_pos-acm;
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
	return p;
}

std::string_view Strategy_PLFromPos::getID() const {
	return id;
}

IStrategy* Strategy_PLFromPos::reset() const {
	return new Strategy_PLFromPos(cfg);
}

IStrategy* Strategy_PLFromPos::setMarketInfo(
		const IStockApi::MarketInfo &minfo) const {
	if (minfo.invert_price) {
		Config cfg = this->cfg;
		cfg.neutral_pos = -this->cfg.neutral_pos;
		return new Strategy_PLFromPos(cfg, p, pos);
	} else {
		return const_cast<Strategy_PLFromPos *>(this);
	}
}
