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
	return cfg.step / (p * 0.01);
}

double Strategy_PLFromPos::calcNewPos(double tradePrice) const {
	double k = calcK();
	double new_pos = pos + (p - tradePrice) * k;
	if (new_pos * pos > 0) {
		double absdf = fabs(new_pos) - fabs(pos);
		if (absdf < 0)
			new_pos = new_pos - sgn(new_pos) * sqrt(-absdf);
	}
	return new_pos;
}

std::pair<Strategy_PLFromPos::OnTradeResult, IStrategy*> Strategy_PLFromPos::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	double k = calcK();
	//double P = pos / k + p;
	double new_pos = calcNewPos(tradePrice);
	if (cfg.maxpos && std::fabs(new_pos) >=cfg.maxpos) {
		new_pos = sgn(new_pos) * cfg.maxpos;
	}
	double ef = (1/ (2*k)) *(pow2(new_pos) - pow2(pos)) + pos * (tradePrice - p);
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
	double new_pos = calcNewPos(price);
	return new_pos + cfg.neutral_pos + acm - assets;

}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(double assets,
		double currencies) const {
	return MinMax {
		p - sqrt(2*currencies/calcK()),
		assets / calcK() + p
	};
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
