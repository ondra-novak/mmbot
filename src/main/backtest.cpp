#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &cfg, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, std::optional<double> init_pos, double balance, bool neg_bal) {

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
	bool rep;
	for (price = priceSource();price.has_value();price = priceSource()) {
		cont = 0;
		if (std::abs(price->price-bt.price.price) == 0) continue;
		do {
			rep = false;
			bt.event = BTEvent::no_event;
			double p = price->price;
			Ticker tk{p,p,p,price->time};
			double dprice = (p - bt.price.price);
			double pchange = pos * dprice;
			pl = pl + pchange;
			double prev_bal = balance;
			if (minfo.leverage) balance += pchange;

			double dir = p>bt.price.price?-1:1;
			s.onIdle(minfo,tk,pos,balance);
			double mult = dir>0?cfg.buy_mult:cfg.sell_mult;
			double adjbal = std::max(balance,0.0);
			Strategy::OrderData order = s.getNewOrder(minfo, p, p, dir, pos, adjbal,false);
			bool allowAlert = true;
			if (cfg.zigzag && !trades.empty()){
				const auto &l = trades.back();
				if (order.size * l.size < 0 && std::abs(order.size)<std::abs(l.size)) {
					order.size = -l.size;
				}
			}
			Strategy::adjustOrder(dir, mult, allowAlert, order);

			order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
			if (cfg.max_balance.has_value()) {
				if (pos > *cfg.max_balance) order.size = 0;
				else if (order.size + pos > *cfg.max_balance) order.size = *cfg.max_balance - pos;
			}
			if (cfg.min_balance.has_value()) {
				if (pos < *cfg.max_balance) order.size = 0;
				else if (order.size + pos < *cfg.max_balance) order.size = *cfg.min_balance - pos;
			}
			if (minfo.leverage) {
				double max_lev = cfg.max_leverage?std::min(cfg.max_leverage,minfo.leverage):minfo.leverage;
				double max_abs_pos = (adjbal * max_lev)/bt.price.price;
				double new_pos = std::abs(pos + order.size);
				if (new_pos > max_abs_pos) {
					if (cfg.accept_loss) {
						s.reset();
						s.onIdle(minfo, tk, pos, adjbal);
						bt.event = BTEvent::accept_loss;
					} else {
						bt.event = BTEvent::margin_call;
					}
					order.size = 0;
				}
			}
			if (std::abs(order.size) < minsize) {
				order.size = 0;
			}
			if (cfg.max_size && std::abs(order.size) > cfg.max_size) {
				order.size = cfg.max_size*sgn(order.size);
			}
			if (!minfo.leverage) {
				double chg = order.size*p;
				if (balance - chg < 0 || pos + order.size < 0) {
					if (neg_bal) {
						bt.event = BTEvent::no_balance;
					} else {
						if (cfg.accept_loss) {
							s.reset();
							s.onIdle(minfo, tk, pos, adjbal);
							bt.event = BTEvent::accept_loss;
						}
						order.size = 0;
						chg = 0;
					}
				}
				balance -= chg;
				pos += order.size;
			} else {
				if (balance <= 0 && prev_bal > 0) {
					bt.event = BTEvent::liquidation;
					order.size -= pos;
				} else {
					if (balance <= 0) {
						bt.event = BTEvent::no_balance;
					}
					else {
						double mb = balance + dprice * (pos + order.size);
						if (mb < 0) {
							bt.event = BTEvent::margin_call;
						}
					}
				}
				pos += order.size;
			}
			auto tres = s.onTrade(minfo, p, order.size, pos, balance);
			bt.neutral_price = tres.neutralPrice;
			bt.norm_accum += tres.normAccum;
			bt.norm_profit += tres.normProfit;
			bt.open_price = tres.openPrice;
			bt.size = order.size;
			bt.price.price = p;
			bt.price.time = price->time;
			bt.pl = pl;
			bt.pos = pos;
			bt.norm_profit_total = bt.norm_profit + bt.norm_accum * p;
			bt.info = s.dumpStatePretty(minfo);
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
