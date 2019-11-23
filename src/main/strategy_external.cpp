/*
 * strategy_external.cpp
 *
 *  Created on: 23. 11. 2019
 *      Author: ondra
 */

#include "strategy_external.h"

#include "imtjson/object.h"


class StrategyExternal::Strategy: public IStrategy {
public:
	Strategy(StrategyExternal &owner, const std::string_view &id, json::Value config, json::Value state);

	virtual bool isValid() const override;
	virtual PStrategy onIdle(const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker, double assets, double currency) const override;
	virtual std::pair<OnTradeResult, PStrategy > onTrade(const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize, double assetsLeft, double currencyLeft) const override;
	virtual json::Value exportState() const override;
	virtual PStrategy importState(json::Value src) const override;
	virtual OrderData getNewOrder(const IStockApi::MarketInfo &minfo, double new_price, double dir, double assets, double currency) const override;
	virtual MinMax calcSafeRange(const IStockApi::MarketInfo &minfo, double assets, double currencies) const override;
	virtual double getEquilibrium() const override;
	virtual PStrategy reset() const override;
	virtual std::string_view getID() const override;

protected:
	StrategyExternal &owner;
	json::Value config;
	json::Value state;
	std::string id;

	json::Object reqHdr() const {
		json::Object o;
		o.set("config", config);
		o.set("state", state);
		return o;
	}
	static json::Value toJSON(const IStockApi::MarketInfo &minfo);
	static json::Value toJSON(const IStockApi::Ticker &minfo);
};


PStrategy StrategyExternal::createStrategy(const std::string_view &id, json::Value config) {
	return new Strategy(*this, id, config, json::object);
}

StrategyExternal::Strategy::Strategy(StrategyExternal &owner,
		const std::string_view &id, json::Value config, json::Value state)
	:owner(owner),config(config),state(state),id(id)
{

}

bool StrategyExternal::Strategy::isValid() const {
	return owner.jsonRequestExchange("isValid", reqHdr()).getBool();
}

PStrategy StrategyExternal::Strategy::onIdle(
		const IStockApi::MarketInfo &minfo, const IStockApi::Ticker &curTicker,
		double assets, double currency) const {

	json::Value st = owner.jsonRequestExchange("onIdle", reqHdr()
							("minfo",toJSON(minfo))
							("ticker", toJSON(curTicker))
							("assets", assets)
							("currency", currency));
	return new Strategy(owner,id,config,st);

}

std::pair<IStrategy::OnTradeResult, PStrategy> StrategyExternal::Strategy::onTrade(
		const IStockApi::MarketInfo &minfo, double tradePrice, double tradeSize,
		double assetsLeft, double currencyLeft) const {

	json::Value res = owner.jsonRequestExchange("onTrade", reqHdr()
							("minfo",toJSON(minfo))
							("price", tradePrice)
							("size",tradeSize)
							("assets", assetsLeft)
							("currency", currencyLeft));

	json::Value st = res["state"];
	OnTradeResult rp;
	rp.normAccum = res["norm_accum"].getNumber();
	rp.normProfit = res["norm_profit"].getNumber();

	return {
		rp,
		new Strategy(owner,id,config,st)
	};

}

json::Value StrategyExternal::Strategy::exportState() const {
	return state;
}

PStrategy StrategyExternal::Strategy::importState(json::Value src) const {
	return new Strategy(owner, id, config, src);
}

StrategyExternal::Strategy::OrderData StrategyExternal::Strategy::getNewOrder(
		const IStockApi::MarketInfo &minfo, double new_price, double dir,
		double assets, double currency) const {
	json::Value ord = owner.jsonRequestExchange("getNewOrder",reqHdr()
			("minfo",toJSON(minfo))
			("new_price", new_price)
			("dir",dir)
			("assets",assets)
			("currency", currency));
	return OrderData {
		ord["price"].getNumber(),
		ord["size"].getNumber()
	};

}

IStrategy::MinMax StrategyExternal::Strategy::calcSafeRange(
		const IStockApi::MarketInfo &minfo, double assets,
		double currencies) const {

	json::Value mm = owner.jsonRequestExchange("calcSafeRange",reqHdr()
			("minfo",toJSON(minfo))
			("assets",assets)
			("currency", currencies));

	return MinMax {
		mm["min"].getNumber(),
		mm["max"].getNumber()
	};

}

double StrategyExternal::Strategy::getEquilibrium() const {
	json::Value mm = owner.jsonRequestExchange("getEquilibrium",reqHdr());
	return mm.getNumber();
}

PStrategy StrategyExternal::Strategy::reset() const {
	return new Strategy(owner, id, config, json::object);
}

std::string_view StrategyExternal::Strategy::getID() const {
	return id;
}

json::Value StrategyExternal::Strategy::toJSON(const IStockApi::MarketInfo &minfo) {
	return json::Object
			("asset_symbol",minfo.asset_symbol)
			("currency_symbol",minfo.currency_symbol)
			("asset_step",minfo.asset_step)
			("currency_step",minfo.currency_step)
			("min_size",minfo.min_size)
			("min_volume",minfo.min_volume)
			("leverage",minfo.leverage)
			("invert_price",minfo.invert_price)
			("inverted_symbol",minfo.inverted_symbol)
			("simulator",minfo.simulator);

}

json::Value StrategyExternal::Strategy::toJSON(const IStockApi::Ticker &tk) {
	return json::Object
			("ask",tk.ask)
			("bid",tk.bid)
			("last",tk.last)
			("time",tk.time);
}
