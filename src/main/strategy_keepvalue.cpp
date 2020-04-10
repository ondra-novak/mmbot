/*
 * strategy_keepvalue.cpp
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#include "strategy_keepvalue.h"

#include <chrono>
#include <imtjson/object.h>
#include "../shared/logOutput.h"
#include <cmath>

using ondra_shared::logDebug;

#include "../shared/logOutput.h"
std::string_view Strategy_KeepValue::id = "keepvalue";

Strategy_KeepValue::Strategy_KeepValue(const Config &cfg, State &&st)
:cfg(cfg), st(std::move(st)) {}


bool Strategy_KeepValue::isValid() const {
	return st.valid && calcK() > 0;
}

double Strategy_KeepValue::calcK() const {
	double k = st.p * (st.a + cfg.ea);
	double tm = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - st.lt).count();
	static double month_sec = 24*60*60*30;
	double f = tm / month_sec;
	return std::max(k + cfg.chngtm * f,0.0);

}

PStrategy Strategy_KeepValue::onIdle(
		const IStockApi::MarketInfo &,
		const IStockApi::Ticker &ticker, double assets, double) const {
	if (isValid()) return this;
	else return new Strategy_KeepValue(cfg, State{true,ticker.last, assets,std::chrono::system_clock::now()});
}

std::pair<Strategy_KeepValue::OnTradeResult, PStrategy> Strategy_KeepValue::onTrade(
		const IStockApi::MarketInfo &minfo,
		double tradePrice, double tradeSize, double assetsLeft,
		double currencyLeft) const {

	if (!st.valid) {
		Strategy_KeepValue tmp(cfg, State{true,tradePrice, assetsLeft-tradeSize,std::chrono::system_clock::now()});
		return tmp.onTrade(minfo, tradePrice,tradeSize,assetsLeft, currencyLeft);
	}

	double p = st.p;
	double k = calcK();
	double neutral = (tradePrice*std::exp(-currencyLeft/k+1));
	double autoaccum = cfg.keep_half?(tradePrice<neutral?0:cfg.accum):cfg.accum;

	double cf = (assetsLeft-tradeSize+cfg.ea)*(tradePrice - p);
	double nv = k * std::log(tradePrice/p);
	double pf = cf - nv;
	double ap = (pf / tradePrice) * autoaccum;
	double np = pf * (1.0 - autoaccum);
	double new_a = (calcK() / tradePrice) - cfg.ea;
	return {
		OnTradeResult{np,ap,neutral},
		new Strategy_KeepValue(cfg,  State{true,tradePrice, new_a+ap,std::chrono::system_clock::now()})
	};

}

json::Value Strategy_KeepValue::exportState() const {
	return json::Object
			("p",st.p)
			("a",st.a	)
			("valid",st.valid)
			("lt",static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(st.lt.time_since_epoch()).count()));
}

PStrategy Strategy_KeepValue::importState(json::Value src) const {
	State newst;
	newst.p = src["p"].getNumber();
	newst.a = src["a"].getNumber();
	std::uint64_t t = src["lt"].getUIntLong();
	newst.valid = src["valid"].getBool();
	newst.lt = std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(t));
	return new Strategy_KeepValue(cfg, std::move(newst));
}

IStrategy::OrderData Strategy_KeepValue::getNewOrder(
		const IStockApi::MarketInfo &,
		double, double price, double /*dir*/, double assets, double /*currency*/) const {
	double k = calcK();
	double na = k / price;
	return {
		0,
		calcOrderSize(st.a, assets, na - cfg.ea)
	};
}

Strategy_KeepValue::MinMax Strategy_KeepValue::calcSafeRange(
		const IStockApi::MarketInfo &,
		double assets,
		double currencies) const {
	double k = st.p*(st.a+cfg.ea);
	double n = st.p*std::exp(-currencies/k);
	double m = cfg.ea > 0?(k/cfg.ea):std::numeric_limits<double>::infinity();
	return MinMax {n,m};
}

double Strategy_KeepValue::getEquilibrium() const {
	return  st.p;
}

std::string_view Strategy_KeepValue::getID() const {
	return id;

}

PStrategy Strategy_KeepValue::reset() const {
	return new Strategy_KeepValue(cfg,{});
}

json::Value Strategy_KeepValue::dumpStatePretty(
		const IStockApi::MarketInfo &minfo) const {

	return json::Object("Assets", st.a)
				 ("Last price", st.p)
				 ("Keep", calcK());

}


