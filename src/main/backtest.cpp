#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &cfg, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, std::optional<double> init_pos, double balance, bool fill_atprice) {

	std::optional<BTPrice> price = priceSource();
	if (!price.has_value()) return {};
	BTTrades trades;

	Strategy s = cfg.strategy;

	BTTrade bt;
	bt.price = *price;

	double pos;
	if (init_pos.has_value() ) {
		pos = *init_pos;
		if (minfo.invert_price) pos = -pos;
	}else {
		pos = s.calcInitialPosition(minfo,bt.price.price,0,balance);
		if (!minfo.leverage) balance -= pos * bt.price.price;
	}

	trades.push_back(bt);

	double pl = 0;
	double minsize = std::max(minfo.min_size, cfg.min_size);
	int cont = 0;
	const std::uint64_t sliding_spread_wait = cfg.spread_calc_sma_hours *50000;
	const std::uint64_t delayed_alert_wait = cfg.accept_loss * 3600000;
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
			if (minfo.leverage) balance = balance + pchange;
			double dir = p>bt.price.price?-1:1;
			bool checksl = false;
			if (balance > 0) {
				s.onIdle(minfo,tk,pos,balance);
				double mult = dir>0?cfg.buy_mult:cfg.sell_mult;
				Strategy::OrderData order = s.getNewOrder(minfo, p, p, dir, pos, balance);
				bool allowAlert = (cfg.alerts || (cfg.dynmult_sliding && price->time - bt.price.time > sliding_spread_wait))
						|| (cfg.delayed_alerts &&  price->time - bt.price.time >delayed_alert_wait);
				checksl = order.alert == IStrategy::Alert::forced || order.alert == IStrategy::Alert::stoploss;
				if (cfg.zigzag && !trades.empty()){
					const auto &l = trades.back();
					if (order.size * l.size < 0 && std::abs(order.size)<std::abs(l.size)) {
						order.size = -l.size;
					}
				}
				Strategy::adjustOrder(dir, mult, allowAlert, order);
				order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
				auto min_pos = cfg.min_balance;
				auto max_pos = cfg.max_balance;
				if (minfo.leverage) {
					double max_abs_pos = (balance * minfo.leverage)/bt.price.price;
					if (!max_pos.has_value() || max_abs_pos < *max_pos) max_pos = max_abs_pos;
					if (!min_pos.has_value() || -max_abs_pos > *min_pos) min_pos = -max_abs_pos;
				}
				if (max_pos.has_value() && pos+order.size > max_pos)
					order.size = std::max(0.0, *max_pos - pos);
				if (min_pos.has_value() && pos+order.size < min_pos)
					order.size = std::min(0.0, *min_pos - pos);
				if (std::abs(order.size) < minsize) {
					order.size = 0;
				}
				if (cfg.max_size && std::abs(order.size) > cfg.max_size) {
					order.size = cfg.max_size*sgn(order.size);
				}
				if (!minfo.leverage) {
					double chg = order.size*p;
					if (balance - chg < 0 || pos + order.size < 0) {
						order.size = 0;
						chg = 0;
					}
					balance -= chg;
					pos += order.size;
				} else {
					pos += order.size;
				}
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
				pos = 0;
			}
			bt.price.price = p;
			bt.price.time = price->time;
			bt.pl = pl;
			bt.pos = pos;
			bt.norm_profit_total = bt.norm_profit + bt.norm_accum * p;
			bt.info = s.dumpStatePretty(minfo);
			trades.push_back(bt);
			if (checksl) {
				if (fill_atprice) {
					Strategy::OrderData order = s.getNewOrder(minfo, bt.price.price, bt.price.price*(1+0.1*dir) , -dir, pos, balance);
					Strategy::adjustOrder(-dir, 1.0,false, order);
					if (order.price == bt.price.price && order.size) {
						order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
						if (std::abs(order.size) >= minsize) {
							pos += order.size;
							if (!minfo.leverage) balance -= order.size*p;
							auto tres = s.onTrade(minfo, p, order.size, pos, balance);
							bt.neutral_price = tres.neutralPrice;
							bt.norm_accum += tres.normAccum;
							bt.norm_profit += tres.normProfit;
							bt.open_price = tres.openPrice;
							bt.size = order.size;
							bt.pl = pl;
							bt.pos = pos;
							trades.push_back(bt);
						}
					}
				}
			}


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
