
/*
 * swap_broker.cpp
 *
 *  Created on: 4. 8. 2020
 *      Author: ondra
 */

#include "swap_broker.h"

#include <cmath>

#include "sgn.h"

static double round_fn(double x) {
	return std::round(x);
}


static double tozero_fn(double x) {
	if (x > 0) return std::floor(x);
	else return std::ceil(x);
}

static double fromzero_fn(double x) {
	if (x > 0) return std::ceil(x);
	else return std::floor(x);
}

static double floor_fn(double x) {
	return std::floor(x);
}
static double no_price_conv(double x) {
	return x;
}
static double inv_price_conv(double x) {
	return 1.0/x;
}
static void no_price_conv_tk(IStockApi::Ticker &) {

}
static void inv_price_conv_tk(IStockApi::Ticker &x) {
	std::swap(x.bid, x.ask);
	x.bid = 1.0/x.bid;
	x.ask = 1.0/x.ask;
	x.last = 1.0/x.last;
}

template<typename Fn>
IStockApi::Orders SwapBrokerBase::getOpenOrders_p(Fn && priceConv, const std::string_view &par) {
	std::lock_guard _(mx);
	ords = target->getOpenOrders(par);
	Orders new_orders;
	std::transform(ords.begin(), ords.end(), std::back_inserter(new_orders), [&](const Order &ord){
		return Order{
			ord.id,
			ord.client_id,
			-ord.size * ord.price,
			priceConv(ord.price)
		};
	});
	return new_orders;

}
template<typename Fn>
IStockApi::TradesSync SwapBrokerBase::syncTrades_p(Fn && priceConv, json::Value lastId, const std::string_view &pair) {
	std::lock_guard _(mx);
	IStockApi::TradesSync data = target->syncTrades(lastId, pair);
	std::transform(data.trades.begin(), data.trades.end(), data.trades.begin(), [&](const Trade &tr){
		return Trade {
			tr.id,
			tr.time,
			-tr.price*tr.size,
			priceConv(tr.price),
			-tr.eff_price*tr.eff_size,
			priceConv(tr.eff_price)
		};
	});
	return data;

}
template<typename Fn>
IStockApi::Ticker SwapBrokerBase::getTicker_p(Fn && priceConv, const std::string_view &pair) {
	Ticker tk = target->getTicker(pair);
	priceConv(tk);
	return tk;

}
template<typename Fn>
void SwapBrokerBase::batchPlaceOrder_p(Fn && priceConv, const NewOrderList &orders, ResultList &result) {
	std::lock_guard _(mx);
	convord.clear();
	filtered.clear();
	result.clear();

	for(const NewOrder &x: orders) {
		double new_price = minfo.adjValue(priceConv(x.price), minfo.currency_step, round_fn);
		double new_size = minfo.adjValue(-x.size / new_price, minfo.asset_step, fromzero_fn);

		double new_replace = 0;
		if (x.replace_order_id.hasValue()) {
			auto iter = std::find_if(ords.begin(), ords.end(), [&](const Order &ord){
				return ord.id == x.replace_order_id;
			});
			if (iter != ords.end()) {
				if (iter->client_id == x.client_id && std::abs(iter->price - new_price)<minfo.currency_step && std::abs(iter->size -new_size) < minfo.asset_step) {
					filtered.push_back(iter->id);
					continue;
				}
			}
			new_replace = minfo.adjValue(x.replace_excepted_size / iter->price, minfo.asset_step, tozero_fn);
		}
		if (std::abs(new_size) < minfo.min_size) {
			new_size = minfo.min_size * sgn(new_size);
		}

		if (new_size > 0) {
			double remain = getBalance(minfo.currency_symbol, x.symbol)/new_price;
			remain -= minfo.asset_step;
			new_size = minfo.adjValue(std::min(remain, new_size), minfo.asset_step, floor_fn);
			if (std::abs(new_size) < minfo.getMinSize(new_price)) {
				new_size = sgn(new_size * minfo.getMinSize(new_price));
				new_size = minfo.adjValue(std::min(remain, new_size), minfo.asset_step, fromzero_fn);
			}
		}
		filtered.push_back(json::undefined);
		convord.push_back(NewOrder{
			x.symbol, new_size, new_price, x.client_id, x.replace_order_id, new_replace
		});
	}
	convord_ret.clear();
	target->batchPlaceOrder(convord, convord_ret);
	std::size_t i = 0;
	for (const json::Value x: filtered) {
		if (x.hasValue()) {
			result.push_back({x,json::Value()});
		}
		else {
			result.push_back(convord_ret[i]);
			++i;
		}
	}

}

json::Value SwapBrokerBase::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	std::lock_guard _(mx);

	tmp_ord.clear();
	tmp_ord.push_back(NewOrder{pair, size, price, replaceId, replaceSize});
	batchPlaceOrder(tmp_ord, tmp_ret);
	if (!tmp_ret[0].error.hasValue()) throw std::runtime_error(tmp_ret[0].error.getString());
	else return tmp_ret[0].order_id;
}


IStockApi::MarketInfo InvertBroker::getMarketInfo(const std::string_view &pair) {
	return getMarketInfoAndTicker(pair).first;
}

std::pair<IStockApi::MarketInfo,IStockApi::Ticker> SwapBrokerBase::getMarketInfoAndTicker(const std::string_view &pair) {
	std::lock_guard _(mx);
	minfo = target->getMarketInfo(pair);
	Ticker tk = target->getTicker(pair);
	if (minfo.leverage || minfo.type != MarketType::normal) throw std::runtime_error("Can't swap assets and currencies on leveraged markets");
	return {MarketInfo {
		minfo.currency_symbol,
		minfo.asset_symbol,
		tk.last * minfo.asset_step,
		minfo.currency_step,
		minfo.min_volume,
		minfo.min_size,
		minfo.fees,
		minfo.feeScheme==FeeScheme::assets?FeeScheme::currency:minfo.feeScheme==FeeScheme::currency?FeeScheme::assets:minfo.feeScheme,
		0,
		false,
		minfo.currency_symbol,
		minfo.simulator,
		minfo.private_chart,
		minfo.wallet_id,
		MarketType::inverted
	},tk};
}

IStockApi::MarketInfo SwapBroker::getMarketInfo(const std::string_view &pair) {
	auto x = SwapBrokerBase::getMarketInfoAndTicker(pair);
	x.first.type = MarketType::normal;
	x.first.currency_step = std::abs(1/x.second.last - 1/(x.second.last+minfo.currency_step));
	return x.first;
}


void SwapBrokerBase::reset(const std::chrono::system_clock::time_point &tp) {
	target->reset(tp);
}

double SwapBrokerBase::getBalance(const std::string_view &symb, const std::string_view &pair) {
	return target->getBalance(symb, pair);
}

IStockApi::TradesSync InvertBroker::syncTrades(json::Value lastId, const std::string_view &pair) {
	return syncTrades_p(no_price_conv, lastId, pair);
}


IStockApi::Orders InvertBroker::getOpenOrders(const std::string_view &pair) {
	return getOpenOrders_p(no_price_conv, pair);
}



void InvertBroker::batchPlaceOrder(const NewOrderList &orders, ResultList &result)  {
	batchPlaceOrder_p(no_price_conv, orders,result);
}



IStockApi::Ticker InvertBroker::getTicker(const std::string_view &pair) {
	return getTicker_p(no_price_conv_tk, pair);
}


IStockApi::TradesSync SwapBroker::syncTrades(json::Value lastId, const std::string_view &pair) {
	return syncTrades_p(inv_price_conv, lastId, pair);
}


IStockApi::Orders SwapBroker::getOpenOrders(const std::string_view &pair) {
	return getOpenOrders_p(inv_price_conv, pair);
}



void SwapBroker::batchPlaceOrder(const NewOrderList &orders, ResultList &result)  {
	batchPlaceOrder_p(inv_price_conv, orders,result);
}



IStockApi::Ticker SwapBroker::getTicker(const std::string_view &pair) {
	return getTicker_p(inv_price_conv_tk, pair);
}


