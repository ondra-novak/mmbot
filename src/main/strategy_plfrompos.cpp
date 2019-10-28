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
	double k = calcK();
	double new_pos;
	if (pos == 0 || (p - tradePrice) / pos > 0 ) new_pos = pos + (p - tradePrice) * k;
	else {
		double inr = 1 +  2*k*(p - tradePrice)/pos;
		new_pos = inr>=0?pos * sqrt(inr):0;
	}
	return new_pos;
}

std::pair<Strategy_PLFromPos::OnTradeResult, IStrategy*> Strategy_PLFromPos::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	double k = calcK();
	double new_pos = calcNewPos(tradePrice,true);
	double act_pos = assetsLeft-acm-cfg.neutral_pos;
	double prev_pos = act_pos - tradeSize;
	if (cfg.maxpos && std::fabs(act_pos) >=cfg.maxpos) {
		new_pos = sgn(act_pos) * cfg.maxpos;
	}
	double ef = (1/ (2*k)) *(pow2(act_pos) - pow2(prev_pos)) + prev_pos * (tradePrice - p);
	double np = ef * (1 - cfg.accum);
	double ap = ef * cfg.accum;
	return {
		OnTradeResult{np,ap},
		new Strategy_PLFromPos(cfg,tradePrice, new_pos, acm+ap)
	};
}

json::Value Strategy_PLFromPos::exportState() const {
	return json::Object
			("p",p)
			("pos",pos)
			("acm",acm);

}

IStrategy* Strategy_PLFromPos::importState(json::Value src) const {
	double new_p = src["p"].getNumber();
	double new_pos = src["pos"].getNumber();
	double new_acm =  src["acm"].getNumber();
	return new Strategy_PLFromPos(cfg, new_p, new_pos, new_acm);
}

double Strategy_PLFromPos::calcOrderSize(double price, double assets) const {
	bool reducepos = cfg.maxpos == 0 || std::fabs(assets-acm-cfg.neutral_pos) < cfg.maxpos;
	double new_pos = calcNewPos(price, reducepos);
	return new_pos + cfg.neutral_pos + acm - assets;

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
