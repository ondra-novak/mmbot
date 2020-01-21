#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &config, BTPriceSource &&priceSource, const IStockSelector &ssel, double init_pos, double balance) {

	IStockApi *api = ssel.getStock(config.broker);
	if (api == nullptr) return {};

	auto minfo = api->getMarketInfo(config.pairsymb);

	std::optional<BTPrice> price = priceSource();
	if (!price.has_value()) return {};
	double pos = init_pos;
	BTTrades trades;

	Strategy s = config.strategy;
	s.onIdle(minfo, Ticker {price->price,price->price,price->price,price->time},pos,balance);

	BTTrade bt;
	bt.price = *price;
	trades.push_back(bt);

	double pl = 0;

	double minsize = std::max(minfo.min_size, config.min_size);
	for (price = priceSource();price.has_value();price = priceSource()) {
		double pchange = pos * (price->price - bt.price.price);;
		pl = pl + pchange;
		balance = balance + pchange;
		if (balance > 0) {
			double dir = sgn(bt.price.price- price->price);
			auto orderData = s.getNewOrder(minfo, bt.price.price, price->price, dir, pos, balance);
			if (orderData.size * dir <= 0) {
				if (config.dust_orders) {
					orderData.size = dir * minsize;
				}else{
					continue;
				}
			}
			orderData.size *= dir>0?config.buy_mult:config.sell_mult;
			orderData.size  = IStockApi::MarketInfo::adjValue(orderData.size,minfo.asset_step,round);
			if (std::abs(orderData.size) < minsize)
				continue;
			pos += orderData.size;
			auto tres = s.onTrade(minfo, price->price, orderData.size, pos, balance);
			bt.neutral_price = tres.neutralPrice;
			bt.norm_accum += tres.normAccum;
			bt.norm_profit += tres.normProfit;
			bt.size = orderData.size;
		} else {
			bt.neutral_price = 0;
			bt.norm_accum += 0;
			bt.norm_profit += 0;
			bt.size = 0;
			pl-=balance;
			balance = 0;
			pos = 0;
		}
		bt.price = *price;
		bt.pl = pl;
		bt.pos = pos;
		trades.push_back(bt);
	}

	if (minfo.invert_price) {
		for (auto &&x: trades) {
			x.neutral_price = 1.0/x.neutral_price;
			x.pos = -x.pos;
			x.price.price = 1.0/x.price.price;
			x.size = -x.size;
		}
	}

	return trades;
}
