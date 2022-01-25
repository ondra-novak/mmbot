
/*
 * swap_broker.cpp
 *
 *  Created on: 4. 8. 2020
 *      Author: ondra
 */

#include "swap_broker.h"

#include <cmath>

#include "sgn.h"
SwapBroker::SwapBroker(PStockApi target):target(target) {
	// TODO Auto-generated constructor stub

}


std::vector<std::string> SwapBroker::getAllPairs() {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return {};
	return sub->getAllPairs();
}

IStockApi::MarketInfo SwapBroker::getMarketInfo(const std::string_view &pair) {
	minfo = target->getMarketInfo(pair);
	if (minfo.leverage || minfo.invert_price) throw std::runtime_error("Can't swap assets and currencies on leveraged markets");
	return MarketInfo {
		minfo.currency_symbol,
		minfo.asset_symbol,
		minfo.currency_step*minfo.asset_step,
		minfo.currency_step,
		minfo.min_volume,
		minfo.min_size,
		minfo.fees,
		minfo.feeScheme==assets?currency:minfo.feeScheme==currency?assets:minfo.feeScheme,
		0,
		true,
		minfo.currency_symbol,
		minfo.simulator,
		minfo.private_chart,
		minfo.wallet_id
	};
}


IBrokerControl::PageData SwapBroker::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const IBrokerControl::PageData &pageData) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) throw std::runtime_error("unsupported operation");
	return sub->fetchPage(method, vpath, pageData);
}

json::Value SwapBroker::getSettings(const std::string_view &pairHint) const {
	auto sub = dynamic_cast<const IBrokerControl *>(target.get());
	if (sub == nullptr) return json::Value();
	return sub->getSettings(pairHint);

}

SwapBroker::BrokerInfo SwapBroker::getBrokerInfo() {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return {};
	BrokerInfo binfo = sub->getBrokerInfo();
	return BrokerInfo {
		binfo.trading_enabled,
		binfo.name,
		binfo.exchangeName,
		binfo.exchangeUrl,
		binfo.version,
		binfo.licence,
		binfo.favicon,
		binfo.settings && dynamic_cast<const IBrokerControl *>(target.get()) != nullptr,
		binfo.subaccounts && dynamic_cast<const IBrokerSubaccounts *>(target.get()) != nullptr,
	};
}


double SwapBroker::getBalance(const std::string_view &symb, const std::string_view &pair) {
	return target->getBalance(symb, pair);
}

json::Value SwapBroker::setSettings(json::Value v) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return json::Value();
	return sub->setSettings(v);

}

void SwapBroker::restoreSettings(json::Value v) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return ;
	return sub->restoreSettings(v);
}

IStockApi::TradesSync SwapBroker::syncTrades(json::Value lastId, const std::string_view &pair) {
	IStockApi::TradesSync data = target->syncTrades(lastId, pair);
	std::transform(data.trades.begin(), data.trades.end(), data.trades.begin(), [](const Trade &tr){
		return Trade {
			tr.id,
			tr.time,
			-tr.price*tr.size,
			1.0/tr.price,
			-tr.eff_price*tr.eff_size,
			1.0/tr.eff_price
		};
	});
	return data;
}

bool SwapBroker::reset() {
	return target->reset();
}

IStockApi::Orders SwapBroker::getOpenOrders(const std::string_view &par) {
	ords = target->getOpenOrders(par);
	Orders new_orders;
	std::transform(ords.begin(), ords.end(), std::back_inserter(new_orders), [](const Order &ord){
		return Order{
			ord.id,
			ord.client_id,
			-ord.size * ord.price,
			1.0/ord.price
		};
	});
	return new_orders;
}

static double round_fn(double x) {
	return std::round(x);
}
static double tozero_fn(double x) {
	if (x > 0) return std::floor(x);
	else return std::ceil(x);
}

static double floor_fn(double x) {
	return std::floor(x);
}

json::Value SwapBroker::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	double new_size = minfo.adjValue(-size * price, minfo.asset_step, tozero_fn);
	double new_price = price?minfo.adjValue(1.0/price, minfo.currency_step, round_fn):0;

	double new_replace = 0;
	if (replaceId.hasValue()) {
		auto iter = std::find_if(ords.begin(), ords.end(), [&](const Order &ord){
			return ord.id == replaceId;
		});
		if (iter != ords.end()) {
			if (iter->client_id == clientId && std::abs(iter->price - new_price)<minfo.currency_step && std::abs(iter->size -new_size) < minfo.asset_step)
				return iter->id;
		}
		new_replace = minfo.adjValue(replaceSize / iter->price, minfo.asset_step, tozero_fn);
	}
	if (std::abs(new_size) < minfo.min_size) {
		new_size = minfo.min_size * sgn(new_size);
	}

	if (new_size > 0) {
		double remain = getBalance(minfo.currency_symbol, pair)/new_price;
		remain -= minfo.asset_step;
		new_size = minfo.adjValue(std::min(remain, new_size), minfo.asset_step, floor_fn);
		if (new_size < minfo.min_size) return nullptr;
		if (new_size < minfo.min_volume/new_price) return nullptr;
	}
	return target->placeOrder(pair, new_size, new_price, clientId, replaceId, new_replace);
}

double SwapBroker::getFees(const std::string_view &pair) {
	return target->getFees(pair);
}

IStockApi::Ticker SwapBroker::getTicker(const std::string_view &pair) {
	Ticker tk = target->getTicker(pair);
	return Ticker{
		1.0/tk.ask,
		1.0/tk.bid,
		1.0/tk.last,
		tk.time
	};
}


json::Value SwapBroker::getMarkets() const {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return json::object;
	return sub->getMarkets();
}

SwapBroker::AllWallets SwapBroker::getWallet()  {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return {};
	return sub->getWallet();
}

