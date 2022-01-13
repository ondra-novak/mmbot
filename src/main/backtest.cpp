#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>

#include "../imtjson/src/imtjson/object.h"
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &cfg, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, std::optional<double> init_pos, double balance, bool neg_bal, bool spend) {

	BTTrades trades;
	try {
		std::optional<BTPrice> price = priceSource();
		if (!price.has_value()) return trades;

		Strategy s = cfg.strategy;

		BTTrade bt;
		bt.price = price->price;
		bt.time = price->time;

		double pos;
		if (init_pos.has_value() ) {
			pos = *init_pos;
			if (minfo.invert_price) pos = -pos;
		}else {
			pos = s.calcInitialPosition(minfo,bt.price,0,balance);
			if (!minfo.leverage) balance -= pos * bt.price;
			bt.size = pos;
		}

		bt.bal = balance;
		bt.pos = pos;
		trades.push_back(bt);

		double total_spend = 0;
		double pl = 0;
		for (price = priceSource();price.has_value();price = priceSource()) {
			double minsize = std::max(minfo.min_size, cfg.min_size);
			if (std::abs(price->price-bt.price) == 0) continue;
			bt.event = BTEvent::no_event;
			double p = price->price;
			Ticker tk{p,p,p,price->time};
			double prev_bal = balance;
			bool enable_alert = true;

			double eq = s.getCenterPrice(bt.price,pos);
			double dir = p>eq?-1:1;
			s.onIdle(minfo,tk,pos,balance);
			double adjbal = std::max(balance,0.0);
			Strategy::OrderData order = s.getNewOrder(minfo, bt.price*0.9+p*0.1, p, dir, pos, adjbal,false);

			if (order.price) {
				p = order.price;
			}

			double dprice = (p - bt.price);
			double pchange = pos * dprice;
			pl = pl + pchange;
			if (minfo.leverage) balance += pchange;

			if (order.size && order.size * dir < 0) {
				order.size = 0;
			}
			double orgsize = order.size;

			order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
			if (cfg.max_balance.has_value()) {
				if (pos > *cfg.max_balance) order.size = 0;
				else if (order.size + pos > *cfg.max_balance) order.size = *cfg.max_balance - pos;
			}
			if (cfg.min_balance.has_value()) {
				if (pos < *cfg.min_balance) order.size = 0;
				else if (order.size + pos < *cfg.min_balance) order.size = *cfg.min_balance - pos;
			}
			if (minfo.leverage) {
				double max_lev = cfg.max_leverage?std::min(cfg.max_leverage,minfo.leverage):minfo.leverage;
				double max_abs_pos = (adjbal * max_lev)/bt.price;
				double new_pos = std::abs(pos + order.size);
				double cur_pos = std::abs(pos);
				if (new_pos > cur_pos && new_pos > max_abs_pos) {
					if (cfg.accept_loss) {
						s.reset();
						s.onIdle(minfo, tk, pos, adjbal);
						bt.event = BTEvent::accept_loss;
					} else {
						bt.event = BTEvent::margin_call;
					}
					order.size = 0;
					orgsize = 0;
				}
			}
			if (minfo.min_volume) {
				double mvs = minfo.min_volume/bt.price;
				minsize = std::max(minsize, mvs);
			}
			if (order.size && std::abs(order.size) < minsize) {
				if (std::abs(order.size)<minsize*0.5) {
					order.size = 0;
				} else {
					order.size = sgn(order.size)*minsize;
				}
			}
			if (cfg.max_size && std::abs(order.size) > cfg.max_size) {
				order.size = cfg.max_size*sgn(order.size);
			}

			if (cfg.trade_within_budget && order.size * pos > 0 && s.calcCurrencyAllocation(order.size)<0) {
				order.size = 0;
				bt.event = BTEvent::no_balance;
			}


			if (!minfo.leverage) {
				if (order.size+pos < 0) {
					order.size = -pos;
					orgsize = order.size; //if zero - allow alert
				}
				double chg = order.size*p;
				if (balance - chg < 0 || pos + order.size < -(std::abs(pos) + std::abs(order.size))*1e-10) {
					if (neg_bal) {
						bt.event = BTEvent::no_balance;
					} else {
						if (cfg.accept_loss) {
							s.reset();
							s.onIdle(minfo, tk, pos, adjbal);
							bt.event = BTEvent::accept_loss;
						}
						order.size = 0;
						orgsize = 0; //allow alert this time
						chg = 0;
					}
				}
				balance -= chg;
				pos = pos+order.size;
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
				if (order.size == 0 && cfg.max_leverage && cfg.reduce_on_leverage) {
					double maxPos = adjbal/p;
					if (maxPos < std::abs(pos)) {
						maxPos = maxPos*sgn(pos);
						double diff = maxPos - pos;
						order.alert = IStrategy::Alert::disabled;
						order.price = p;
						order.size = diff;
					}
				}
				pos += order.size;
			}

			if (order.size == 0 && orgsize != 0 && order.alert != IStrategy::Alert::forced) {
				enable_alert = false;
			}

			if (enable_alert) {
				auto tres = s.onTrade(minfo, p, order.size, pos, balance);
				bt.neutral_price = tres.neutralPrice;
				double norm_accum = std::isfinite(tres.normAccum)?tres.normAccum:0;
				bt.norm_accum += norm_accum;
				bt.norm_profit += std::isfinite(tres.normProfit)?tres.normProfit:0;
				bt.open_price = tres.openPrice;
				if (order.size*(order.size-norm_accum)>1) order.size -= norm_accum;
				bt.info = s.dumpStatePretty(minfo);
			} else {
				bt.info = json::Object({
					{"Rejected size", orgsize},
					{"Min size", minsize },
					{"Direction", dir},
					{"Equilibrium", eq},
				});
			}
			if (spend) {
				double alloc = s.calcCurrencyAllocation(p);
				if (alloc>0 && alloc<balance) {
					total_spend += balance-alloc;
					balance = alloc;
				}
			}

			bt.size = order.size;
			bt.price = p;
			bt.time = price->time;
			bt.pl = pl;
			bt.pos = pos;
			bt.bal = balance+total_spend;
			bt.unspend_balance= balance;
			bt.norm_profit_total = bt.norm_profit + bt.norm_accum * p;



			trades.push_back(bt);

			if (minfo.leverage) {
				double minbal = std::abs(pos) * p/(2*minfo.leverage);
				if (balance > minbal) {
					double rbal1 = balance + pos * (price->pmin-p);
					double rbal2 = balance + pos * (price->pmax-p);
					bool trig = false;
					if (rbal1 <= minbal) {
						trig = true;
						bt.price = price->pmin;
					} else if (rbal2 <= minbal) {
						trig = true;
						bt.price = price->pmax;
					}
					if (trig) {
						double df = pos * (bt.price - p);
						pl += df;
						balance += df;
						bt.pos = 0;
						bt.bal = balance + total_spend;
						bt.unspend_balance = balance;
						bt.norm_profit_total = 0;
						bt.norm_profit = 0;
						bt.norm_accum = 0;
						bt.event = BTEvent::liquidation;
						bt.size = -pos;
						bt.pl = pl;
						bt.info = json::object;
						trades.push_back(bt);
					}

				}
			}

		}

	} catch (std::exception &e) {
		if (trades.empty()) throw;
		else {
			BTTrade bt = trades.back();
			bt.time+=3600*1000;
			bt.info = json::Object({{"error", e.what()}});
		}
	}

	if (minfo.invert_price) {
		for (auto &&x: trades) {
			x.neutral_price = 1.0/x.neutral_price;
			x.open_price = 1.0/x.open_price;
			x.pos = -x.pos;
			x.price = 1.0/x.price;
			x.size = -x.size;
		}
	}

	return trades;
}
