/*
 * invert_strategy.cpp
 *
 *  Created on: 13. 2. 2022
 *      Author: ondra
 */

#include "invert_strategy.h"
#include <stdexcept>
#include <imtjson/object.h>
InvertStrategy::InvertStrategy(PStrategy target):target(target),inited(false) {

}

InvertStrategy::InvertStrategy(PStrategy target, double collateral, double last_price, double last_pos)
:target(target)
,collateral(collateral)
,last_price(last_price)
,last_pos(last_pos)
,inited(true)
{
}


IStrategy::OrderData InvertStrategy::getNewOrder(
		const IStockApi::MarketInfo &minfo, double cur_price, double new_price,
		double dir, double assets, double currency, bool rej) const {

	auto res = target->getNewOrder(invert(minfo),
			1.0/cur_price, 1.0/new_price, -dir,
			getPosition(assets),
			getBalance(currency,cur_price), rej);

	if (res.price == 0) res.size = -res.size/new_price;
	else {
		res.size = -res.size * res.price;
		res.price = 1.0/res.price;
	}
	return res;
}

std::pair<IStrategy::OnTradeResult, ondra_shared::RefCntPtr<const IStrategy> > InvertStrategy::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	double ca = collateral-calcCurrencyAllocation(last_price, true);
	auto res = target->onTrade(invert(minfo), 1.0/tradePrice, -tradeSize*tradePrice,
			getPosition(assetsLeft),
			getBalance(currencyLeft,tradePrice));

	double new_col = collateral + (assetsLeft - tradeSize) * (tradePrice - last_price);
	PStrategy new_strg = new InvertStrategy(res.second, new_col, tradePrice, assetsLeft);
	double cb = new_col - new_strg->calcCurrencyAllocation(tradePrice, true);
	double removed = cb - ca;
	new_strg = new InvertStrategy(res.second, new_col-removed, tradePrice, assetsLeft);
	return {{removed, 0, 1.0/res.first.neutralPrice,0}, new_strg};
}

PStrategy InvertStrategy::importState(json::Value src, const IStockApi::MarketInfo &minfo) const {
	json::Value state = src["invert_state"];
	json::Value prx = src["invert_proxy"];
	if (state.defined()) {
		double coll = state["coll"].getNumber();
		double last = state["last"].getNumber();
		double pos = state["pos"].getNumber();
		return new InvertStrategy(target->importState(prx, invert(minfo,last)), coll, last,pos);

	} else {
		return new InvertStrategy(target->importState(prx, invert(minfo,1)));
	}

}

IStrategy::MinMax InvertStrategy::calcSafeRange(const IStockApi::MarketInfo &minfo, double assets,double currencies) const {
	auto res = target->calcSafeRange(invert(minfo), getPosition(assets), getBalance(currencies, last_price));
	return {
		1.0/res.max,
		1.0/res.min
	};
}

bool InvertStrategy::isValid() const {
	return inited && target->isValid();
}

json::Value InvertStrategy::exportState() const {
	return json::Object{
		{"invert_proxy", target->exportState()},
		{"invert_state", json::Object {
						{"coll", collateral},
						{"last", last_price},
						{"pos", last_pos}
		}}
	};
}

std::string_view InvertStrategy::getID() const {
	return target->getID();
}

double InvertStrategy::getCenterPrice(double lastPrice, double assets) const {
	return 1.0/target->getCenterPrice(1.0/lastPrice, getPosition(assets));
}

double InvertStrategy::calcInitialPosition(const IStockApi::MarketInfo &minfo,
		double price, double assets, double currency) const {

	auto res = target->calcInitialPosition(invert(minfo),
			1.0/price, getPosition(assets, currency, price), getBalance(currency, price));

	return getAssetsFromPos(res, currency, price);

}

IStrategy::BudgetInfo InvertStrategy::getBudgetInfo() const {
	auto x = target->getBudgetInfo();
	return {
		getCurrencyFromBal(x.total, last_price),
		getAssetsFromPos(x.assets)
	};
}

double InvertStrategy::getEquilibrium(double assets) const {
	return 1.0/target->getEquilibrium(getPosition(assets));
}

double InvertStrategy::calcCurrencyAllocation(double price, bool leveraged) const {
	return getCurrencyFromBal(target->calcCurrencyAllocation(1.0/price, leveraged), price);
}

IStrategy::ChartPoint InvertStrategy::calcChart(double price) const {
	auto x = target->calcChart(1.0/price);
	double b = collateral+last_pos*(price - last_price);
	if (x.valid) {
		return {
			x.valid,
			getAssetsFromPos(x.position, b, price),
			getCurrencyFromBal(x.budget, price),
		};
	} else {
		return {false};
	}
}

PStrategy InvertStrategy::onIdle(const IStockApi::MarketInfo &minfo,
		const IStockApi::Ticker &curTicker, double assets,
		double currency) const {

	if (!minfo.leverage) throw std::runtime_error("Invert require leveraged market");

	if (!inited) {
		return PStrategy(new InvertStrategy(target,currency, curTicker.last, assets))->onIdle(minfo, curTicker, assets,currency);
	} else {

		PStrategy nw = target->onIdle(invert(minfo), IStockApi::Ticker{
			1.0/curTicker.ask, 1.0/curTicker.bid, 1.0/curTicker.last, curTicker.time
		}, getPosition(assets), getBalance(currency, curTicker.last));

		if (nw != target) return new InvertStrategy(nw, collateral, last_price, assets);
		else return this;
	}


}

PStrategy InvertStrategy::reset() const {
	return new InvertStrategy(target->reset());
}

json::Value InvertStrategy::dumpStatePretty(const IStockApi::MarketInfo &minfo) const {
	json::Value out = target->dumpStatePretty(invert(minfo));
	out.setItems({
		{"Invert.Budget", collateral},
		{"Invert.Last price", minfo.invert_price?1.0/last_price:last_price}
	});
	return out;
}

IStockApi::MarketInfo InvertStrategy::invert(const IStockApi::MarketInfo &src, double cur_price) {
	return {
		"",
		"",
		src.asset_step * cur_price,
		src.currency_step,
		src.min_volume,
		src.min_size,
		src.fees,
		src.feeScheme,
		src.leverage,
		!src.invert_price,
		"",
		src.simulator,
		src.private_chart,
		""
	};

}
IStockApi::MarketInfo InvertStrategy::invert(const IStockApi::MarketInfo &src) const {
	return invert(src, last_price);
}

double InvertStrategy::getPosition(double assets) const {
	return getPosition(assets, collateral, last_price);
}
double InvertStrategy::getPosition(double assets, double collateral, double price) {
	return collateral - assets* price;
}

double InvertStrategy::getBalance(double currency, double price)  {
	return currency / price;
}

double InvertStrategy::getAssetsFromPos(double pos, double collateral, double price) {
	return (collateral - pos)/price;
}
double InvertStrategy::getAssetsFromPos(double pos) const {
	return getAssetsFromPos(pos, collateral, last_price);
}

double InvertStrategy::getCurrencyFromBal(double bal, double price) {
	return bal * price;
}
