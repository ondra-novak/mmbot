#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &cfg, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, double init_pos, double balance, bool fill_atprice) {

	std::optional<BTPrice> price = priceSource();
	if (!price.has_value()) return {};
	double pos = init_pos;
	BTTrades trades;

	Strategy s = cfg.strategy;

	BTTrade bt;
	bt.price = *price;
	trades.push_back(bt);

	double pl = 0;
	double minsize = std::max(minfo.min_size, cfg.min_size);
	int cont = 0;
	bool rep;
	for (price = priceSource();price.has_value();price = priceSource()) {
		cont = 0;
		if (std::abs(price->price-bt.price.price) == 0) continue;
		do {
			rep = false;
			double p = price->price;
			Ticker tk{p,p,p,price->time};
			double pchange = pos * (p - bt.price.price);;
			pl = pl + pchange;
			balance = balance + pchange;
			if (balance > 0) {
				s.onIdle(minfo,tk,pos,balance);
				double dir = p>bt.price.price?-1:1;
				double mult = dir>0?cfg.buy_mult:cfg.sell_mult;
				Strategy::OrderData order = s.getNewOrder(minfo, bt.price.price, p, dir, pos, balance);
				Strategy::adjustOrder(dir, mult,cfg.dust_orders, order);
				if (order.alert) {
					if (fill_atprice) {
						Strategy::OrderData rorder = s.getNewOrder(minfo, bt.price.price, 2*bt.price.price - p, -dir, pos, balance);
						Strategy::adjustOrder(-dir, mult,cfg.dust_orders, rorder);
						if (rorder.price == bt.price.price && rorder.size) {
							rorder.size  = IStockApi::MarketInfo::adjValue(rorder.size,minfo.asset_step,round);
							if (std::abs(rorder.size) >= minsize) {
								cont++; rep = true;order = rorder;
								p = bt.price.price;
								balance -= pchange;
								pl -= pchange;
							}
						}
					}
				} else {
					order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
					if (std::abs(order.size) < minsize)
						continue;
				}
				if (cfg.max_size && std::abs(order.size) > cfg.max_size) {
					order.size = cfg.max_size*sgn(order.size);
				}
				pos += order.size;
				auto tres = s.onTrade(minfo, p, order.size, pos, balance);
				bt.neutral_price = tres.neutralPrice;
				bt.norm_accum += tres.normAccum;
				bt.norm_profit += tres.normProfit;
				bt.open_price = tres.openPrice;
				bt.size = order.size;
			} else {
				bt.neutral_price = 0;
				bt.norm_accum += 0;
				bt.norm_profit += 0;
				bt.size = 0;
				pl-=balance;
				balance = 0;
				pos = 0;
			}
			bt.price.price = p;
			bt.price.time = price->time;
			bt.pl = pl;
			bt.pos = pos;
			trades.push_back(bt);
		} while (cont%16 && rep);
	}

	if (minfo.invert_price) {
		for (auto &&x: trades) {
			x.neutral_price = 1.0/x.neutral_price;
			x.open_price = 1.0/x.open_price;
			x.pos = -x.pos;
			x.price.price = 1.0/x.price.price;
			x.size = -x.size;
		}
	}

	return trades;
}
