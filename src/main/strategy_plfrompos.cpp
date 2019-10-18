/*
 * strategy_plfrompos.cpp
 *
 *  Created on: 18. 10. 2019
 *      Author: ondra
 */

#include "strategy_plfrompos.h"
#include <cmath>
#include <imtjson/object.h>
#include "sgn.h"

std::string_view Strategy_PLFromPos::id = "plfrompos";

Strategy_PLFromPos::Strategy_PLFromPos(const Config &cfg, double p, double a, double pos, double err)
	:cfg(cfg),p(p),a(a),pos(pos), err(err)
{
}


bool Strategy_PLFromPos::isValid() const {
	return p > 0;
}

IStrategy* Strategy_PLFromPos::init(double curPrice, double assets, double ) const {
	return new Strategy_PLFromPos(cfg, curPrice, cfg.neutral_pos, assets - cfg.neutral_pos);
}

std::pair<Strategy_PLFromPos::OnTradeResult, IStrategy*> Strategy_PLFromPos::onTrade(
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {


	double chg = calcOrderSize(tradePrice, 0);
	double new_err = fabs(tradeSize) < fabs(chg*2) && tradeSize != 0 && pos != 0?(chg - tradeSize):0;
	double new_pos = pos + chg;
	double new_a_diff = (tradeSize>0?std::sqrt(tradeSize):0 )* cfg.accum;
	double new_a = a + new_a_diff;
	OnTradeResult res{
		(pos * chg < 0?tradeSize * (tradePrice - p):0) - new_a_diff*tradeSize,
		new_a_diff
	};
	Strategy_PLFromPos *newinst =  new Strategy_PLFromPos(cfg, tradePrice, new_a, new_pos, new_err);
	return std::make_pair(res,newinst);

}

json::Value Strategy_PLFromPos::exportState() const {
	return json::Object
			("p",p)
			("a",a)
			("pos",pos)
			("np",cfg.neutral_pos)
			("err",err);
}

IStrategy* Strategy_PLFromPos::importState(json::Value src) const {
	double new_p = src["p"].getNumber();
	double new_a = src["a"].getNumber();
	double new_pos = src["pos"].getNumber();
	double new_err = src["err"].getNumber();
	double old_np =  src["np"].getNumber();
	if (fabs(old_np - cfg.neutral_pos) <= (fabs(old_np) + fabs(cfg.neutral_pos))*1e-5)
		return new Strategy_PLFromPos(cfg, new_p, new_a, new_pos, new_err);
	else {
		return new Strategy_PLFromPos(cfg);
	}
}

double Strategy_PLFromPos::calcOrderSize(double price, double) const {
	double pos_diff = -((price - p) * cfg.step / (price*0.01));
	if (pos_diff * pos < 0 && fabs(pos) > fabs(pos_diff))
			pos_diff += sqrt(fabs(pos_diff)) * sgn(pos_diff);
	return pos_diff + err;

}

Strategy_PLFromPos::MinMax Strategy_PLFromPos::calcSafeRange(double assets,
		double currencies) const {
	return MinMax {
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::quiet_NaN()
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
