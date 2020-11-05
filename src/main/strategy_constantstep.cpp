/*
 * strategy_constantstep.cpp
 *
 *  Created on: 5. 8. 2020
 *      Author: ondra
 */

#include "strategy_constantstep.h"

#include <cmath>
#include "../imtjson/src/imtjson/object.h"
#include "../imtjson/src/imtjson/value.h"
#include "numerical.h"

using json::Value;

std::string_view Strategy_ConstantStep::id = "conststep";


Strategy_ConstantStep::Strategy_ConstantStep(const Config &cfg, State &&st)
	:cfg(cfg),st(std::move(st))
{
}

Strategy_ConstantStep::Strategy_ConstantStep(const Config &cfg)
	:cfg(cfg)
{
}

bool Strategy_ConstantStep::isValid() const {
	return st.m > 0 && st.p > 0 && st.a + cfg.ea > 0;
}

PStrategy Strategy_ConstantStep::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &ticker, double assets,
		double currency) const {
	if (isValid()) return this;
	else {
		return init(minfo,ticker.last, assets,currency);
	}
}

std::pair<IStrategy::OnTradeResult, PStrategy> Strategy_ConstantStep::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {


	double newA = calcA(tradePrice);


	auto csts = calcConsts(st.a+cfg.ea, st.p, st.m);

	double df = (tradePrice - st.p) * csts.k;
	double cf = -tradeSize * tradePrice;
	double na = tradeSize == assetsLeft?0:(cf - df);

	double accum = calcAccumulation(st, cfg, tradePrice);

	State nst (st);
	nst.a = newA + accum;
	nst.f = currencyLeft;
	nst.p = tradePrice;


	return {
		{na, accum},PStrategy(new Strategy_ConstantStep(cfg,std::move(nst)))
	};


}

json::Value Strategy_ConstantStep::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a)
			("f",st.f);
}


double Strategy_ConstantStep::calcAccumulation(const State &st, const Config &cfg, double price) {
	auto cst = calcConsts(st.a+cfg.ea, st.p, st.m);
	double a1 = calcA(cst, st.p);
	double a2 = calcA(cst, price);
	double cf1 = -st.p * a1;
	double cf2 = -price * a2;
	double cfd = cf2 - cf1;
	double bfd = (price - st.p) * cst.k;
	double np = cfd - bfd;
	return (np/price) * cfg.accum;
}

PStrategy Strategy_ConstantStep::importState(json::Value src,
		const IStockApi::MarketInfo &minfo) const {
	State newst {
		src["a"].getNumber(),
		src["p"].getNumber(),
		src["f"].getNumber()
	};
	return new Strategy_ConstantStep(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_ConstantStep::getNewOrder(const IStockApi::MarketInfo &minfo,
		double cur_price, double new_price, double dir, double assets,
		double currency, bool rej) const {
	double newA = calcA(new_price);
	double extra = calcAccumulation(st, cfg, new_price);
	double ordsz = calcOrderSize(st.a, assets, newA+extra);
	return {0,ordsz};
}

IStrategy::MinMax Strategy_ConstantStep::calcSafeRange(const IStockApi::MarketInfo &minfo,
		double assets, double currencies) const {
	auto consts = calcConsts(st.a+cfg.ea, st.p, st.m);
	double max = std::exp((consts.c-cfg.ea)/consts.k-1);
	double min = std::max(0.0, st.p - currencies/consts.k);
	return {min,max};
}

double Strategy_ConstantStep::getEquilibrium(double assets) const {
	double a = assets+cfg.ea;
	auto consts = calcConsts(a, st.p, st.m);
	double p = std::exp((consts.c-a)/consts.k-1);
	return std::max(p,0.0);
}

PStrategy Strategy_ConstantStep::reset() const {
	return new Strategy_ConstantStep(cfg,{});
}

std::string_view Strategy_ConstantStep::getID() const {
	return id;
}

json::Value Strategy_ConstantStep::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {
	auto consts = calcConsts(st.a+cfg.ea, st.p, st.m);
	return json::Object("Assets/Position", (minfo.invert_price?-1:1)*st.a)
				 ("Last price ", minfo.invert_price?1.0/st.p:st.p)
				 ("Power (w)", consts.c)
				 ("Anchor price (k)", consts.k)
				 ("Budget", calcAccountValue(consts, st.p))
				 ("Budget Extra(+)/Debt(-)", minfo.leverage?Value():Value(st.f - consts.k * st.p));

}

PStrategy Strategy_ConstantStep::init(const IStockApi::MarketInfo &m,
		double price, double assets, double cur) const {

	State nst = st;
	double a = assets+cfg.ea;
	double ratio = a * price / (cur+a * price);
	if (!std::isfinite(ratio) || ratio <=0) {
		if (nst.p <= 0) {
			nst.p = price;
			nst.a = assets;
		}
		double mp = price * 2;
		if (nst.m <= 0) {
			if (m.invert_price)
				nst.m = 1.0/mp;
			else
				nst.m = mp;
		}

		if (nst.m < nst.p) {
			nst.m = nst.p*2;
			nst.a = calcInitialPosition(m,price,assets, cur);
		}
	} else {
		double m = numeric_search_r2(price,[&](double x){
			auto consts = calcConsts(assets, price, x);
			if (x <= price) return -10.0;
			double r = price*calcA(consts,price)/calcAccountValue(consts,price);
			return r - ratio;
		});
		nst.m = m;
		nst.p = price;
		nst.a = assets;

	}

	nst.f = cur;
	PStrategy s = new Strategy_ConstantStep(cfg, std::move(nst));
	if (!s->isValid()) throw std::runtime_error("Unable to initialize strategy");
	return s;

}

double Strategy_ConstantStep::calcInitialPosition(const IStockApi::MarketInfo& minfo,  double price, double assets, double currency) const {
	double atot = cfg.ea + assets +  currency/price;
	double mp = price*2;
	if (mp <price) mp = price * 2;
	double m = minfo.invert_price?1.0/mp:mp;
	double k = atot/(1 + std::log(m/price));
	double c =  atot + k * std::log(price);
	double max = std::exp(c/k-1);
	if (price > max) return 0;
	//ratio - how much of budget is actually position;
	double rt = 1-k/(c-k*std::log(price));
	return atot * rt - cfg.ea;

}

IStrategy::BudgetInfo Strategy_ConstantStep::getBudgetInfo() const {
	auto consts = calcConsts(st.a+cfg.ea, st.p, st.m);
	return BudgetInfo {
		calcAccountValue(consts, st.p),
		consts.c
	};
}


Strategy_ConstantStep::Consts Strategy_ConstantStep::calcConsts(double a, double p, double max) {
	double k = max > p?a / std::log(max/p):a;
	double c = a + k + k*std::log(p);
	return {k,c};
}

double Strategy_ConstantStep::calcA(double price) const {
	auto consts = calcConsts(st.a+cfg.ea, st.p, st.m);
	return calcA(consts, price) - cfg.ea;
}

double Strategy_ConstantStep::calcA(const Consts &cst, double price) {
	double max = std::exp(cst.c/cst.k-1);
	if (price > max)
		return 0;
	else return cst.c - cst.k * (std::log(price) + 1);
}

double Strategy_ConstantStep::calcAccountValue(const Consts &cst, double price) {
	return price*(cst.c-cst.k*std::log(price));
}

double Strategy_ConstantStep::calcCurrencyAllocation(double price) const {
	return st.a * price / std::log(st.m/price);
}

Strategy_ConstantStep::ChartPoint Strategy_ConstantStep::calcChart(double price) const {
	auto consts = calcConsts(st.a, st.p, st.m);
	return {
		true,
		calcA(consts,  price),
		calcAccountValue(consts,price)
	};
}
