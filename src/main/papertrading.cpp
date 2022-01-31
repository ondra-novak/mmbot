/*
 * papertrading.cpp
 *
 *  Created on: 16. 1. 2022
 *      Author: ondra
 */


#include "papertrading.h"

#include <chrono>
#include <random>

#include <imtjson/binary.h>
#include "../imtjson/src/imtjson/object.h"


PaperTrading::PaperTrading(PStockApi source)
:state{source}
{
	std::random_device x;
	std::uniform_int_distribution<int> rnd('A','Z');
	for (int i = 0;i<16;i++) random_id.push_back(static_cast<char>(rnd(x)));
}

double PaperTrading::getBalance(const std::string_view &symb, const std::string_view &pair) {
	std::lock_guard _(lock);
	if (state.pair != pair) return 0;

	if (state.minfo.leverage) {
		if (state.minfo.asset_symbol == symb) {
			return state.collateral.getPos();
		} else if (state.minfo.currency_symbol == symb) {
			if (!currency_valid) {
				currency = state.source->getBalance(symb, state.pair);
				currency_valid = true;
			}
			return state.collateral.getUPnL(state.ticker.last) + currency;
		}
	} else {
		if (state.minfo.asset_symbol == symb) {
			if (!asset_valid) {
				asset = state.source->getBalance(symb, state.pair);
				asset_valid = true;
			}
			return asset;
		} else if (state.minfo.currency_symbol == symb) {
			if (!currency_valid) {
				currency = state.source->getBalance(symb, state.pair);
				currency_valid = true;
			}
			return currency;
		}
	}
	return 0;

}


IStockApi::TradesSync AbstractPaperTrading::syncTrades(json::Value lastId,const std::string_view &pair) {
	std::lock_guard _(lock);
	TradeState &st = getState(pair);
	if (st.needLoadWallet) {
		if (lastId.hasValue()) importState(st, lastId);
		st.needLoadWallet = false;
	}

	std::uint64_t from = lastId["l"].getUIntLong();

	auto itr = std::upper_bound(st.trades.begin(), st.trades.end(),  Trade{nullptr,from, 0,0,0,0}, [](const Trade &a, const Trade &b){
		return a.time < b.time;
	});
	TradeHistory out(itr, st.trades.end());

	json::Value r = exportState(st);
	if (!out.empty()) from = out.back().time;
	r.setItems({{"l", from}});

	return TradesSync{
		std::move(out),r
	};
}

IStockApi::MarketInfo AbstractPaperTrading::getMarketInfo(const std::string_view &pair) {

	std::lock_guard _(lock);
	TradeState &st = getState(pair);

	auto newminfo = st.source->getMarketInfo(st.src_pair);
	if (newminfo.leverage && !st.minfo.leverage) {
		st.ticker = st.source->getTicker(pair);
		auto b = st.source->getBalance(newminfo.asset_symbol, st.src_pair);
		st.collateral = ACB(st.ticker.last, b, 0);
	}
	newminfo.simulator = true;
	st.minfo = std::move(newminfo);
	return st.minfo;
}

IStockApi::MarketInfo PaperTrading::getMarketInfo(const std::string_view &pair)  {
	auto nfo = AbstractPaperTrading::getMarketInfo(pair);
	nfo.wallet_id.append("-").append(random_id);
	return nfo;
}

void AbstractPaperTrading::processTrade(TradeState &st, const Trade &t) {

	//if leveraged market
	if (st.minfo.leverage) {
		//update collateral
		st.collateral = st.collateral(t.eff_price, t.eff_size);
		//update wallet
		updateWallet(st, st.minfo.currency_symbol, st.collateral.getRPnL());
		st.collateral = st.collateral.resetRPnL();
	} else {
		updateWallet(st, st.minfo.asset_symbol, t.eff_size);
		updateWallet(st, st.minfo.currency_symbol, - t.eff_size*t.eff_price);
	}

}

bool AbstractPaperTrading::canCreateOrder(const TradeState &st, double price, double size) {
	if (st.minfo.leverage) {
		double equity = st.collateral.getEquity(price) + getRawBalance(st).currency;
		double pos = st.collateral.getPos();
		double val = (pos?st.collateral.getOpen():0)*pos;
		double newval = std::abs(price * size);
		return (newval+val <= equity * st.minfo.leverage);
	} else {
		auto b = getRawBalance(st);
		double newcur = b.currency - price * size;
		double newass = b.asset + size;
		return newass >= -st.minfo.asset_step*0.5 && newcur >= 0;
	}
}

//Trading function is implement in reset()
//it loads ticker and performs simulation of market matching
void AbstractPaperTrading::simulate(TradeState &st) {
	//get ticker
	st.ticker = st.source->getTicker(st.src_pair);

	auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	if (st.minfo.leverage) {
		double eq = st.collateral.getEquity(st.ticker.last) + getRawBalance(st).currency;
		double pos = st.collateral.getPos();
		if (eq<0 && pos) {
			//liquidation
			Trade liqtrade{
				"liquidation-"+std::to_string(now),
				static_cast<std::uint64_t>(now),
				-pos,
				st.ticker.last,
				-pos,
				st.ticker.last,
			};
			st.trades.push_back(liqtrade);
			processTrade(st, liqtrade);
			st.openOrders.clear();
			return;
		}
	}

	TradeHistory trades;
	int cnt = 0;
	//each trade has id <time>-<counter>
	auto itr = std::remove_if(st.openOrders.begin(), st.openOrders.end(),[&](const Order &ord){

		cnt++;
		//matching condition
		//orders below ticker for sell and orders above ticker for buys are matched
		if ((ord.price - st.ticker.last)*ord.size >= 0) {
			//partial execution is not simulated

			char tradeID[100];
			snprintf(tradeID,100,"%lX-%X", now, cnt);
			//create trade from order
			Trade t;
			t.id = tradeID;
			t.time = static_cast<uint64_t>(now) * 1000;
			t.price = ord.price;
			t.size = ord.size;
			t.eff_price = t.price;
			t.eff_size = t.size;
			//simulate fees
			st.minfo.removeFees(t.eff_size, t.eff_price);
			//push to list
			trades.push_back(t);
			return true;

		} else {

			//simulate order in orderbook
			if (ord.size<0) {
				st.ticker.ask = std::max(st.ticker.bid, std::min(st.ticker.ask, ord.price));
			} else if (ord.size > 0) {
				st.ticker.bid = std::min(st.ticker.ask, std::max(st.ticker.bid, ord.price));
			}
			return false;
		}

	});
	//remove executed orders
	st.openOrders.erase(itr, st.openOrders.end());

	//some trades?
	if (!trades.empty()) {

		//sort them by increasing distance from ticker
		std::sort(st.trades.begin(), st.trades.end(),[&](const Trade &a, const Trade &b){
			double da = std::abs(a.price - st.ticker.last);
			double db = std::abs(b.price - st.ticker.last);
			return da < db;
		});

		//process all trades and update balances
		for (const Trade &t: trades) {

			processTrade(st,t);
		}
		//append to list of trades for syncTrades
		std::copy(trades.begin(), trades.end(), std::back_inserter(st.trades));
	}

}

void PaperTrading::updateWallet(const TradeState &st, const std::string_view &symbol, double difference) {
	if (symbol == state.minfo.asset_symbol) asset += difference;
	else if (symbol == state.minfo.currency_symbol) currency += difference;
}


IStockApi::Orders AbstractPaperTrading::getOpenOrders(const std::string_view &pair) {
	std::lock_guard _(lock);
	TradeState &st = getState(pair);
	return st.openOrders;
}

json::Value AbstractPaperTrading::placeOrder(const std::string_view &pair, double size,
		double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {
	std::lock_guard _(lock);

	TradeState &st = getState(pair);


	if (replaceId.hasValue()) {
		auto iter = std::remove_if(st.openOrders.begin(), st.openOrders.end(), [&](const Order &o){
			return o.id == replaceId;
		});
		st.openOrders.erase(iter, st.openOrders.end());
	}
	if (size) {
		if (!canCreateOrder(st, price, size)) throw std::runtime_error("Balance insufficient");
		st.openOrders.push_back({
			++st.orderCounter,
			clientId,
			size,
			price
		});
		return st.orderCounter;
	} else {
		return nullptr;
	}

}

double AbstractPaperTrading::getFees(const std::string_view &pair) {
	std::lock_guard _(lock);
	TradeState &st = getState(pair);
	return st.minfo.fees;
}

IStockApi::Ticker AbstractPaperTrading::getTicker(const std::string_view &pair) {
	std::lock_guard _(lock);
	TradeState &st = getState(pair);
	return st.ticker;
}

json::Value PaperTrading::getMarkets() const {
	return json::Object();
}

std::vector<std::string> PaperTrading::getAllPairs() {
	return {state.pair};
}

void PaperTrading::restoreSettings(json::Value v) {
}

json::Value PaperTrading::setSettings(json::Value v) {
	std::lock_guard _(lock);
	auto jpos = v["position"];
	auto jcur = v["currency"];
	if (state.minfo.leverage) {
		double pos = state.collateral.getPos();
		double nv = jpos.getNumber();
		if (state.minfo.invert_price) nv = -nv;
		double diff = nv - pos;
		if (std::abs(diff) > state.minfo.asset_step) {
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*1000;
			Trade t{
					now,static_cast<std::uint64_t>(now),diff,state.ticker.last,diff,state.ticker.last
			};
			processTrade(state,  t);
			state.trades.push_back(t);
		}
	} else {
		asset = jpos.getNumber();
	}
	currency = jcur.getNumber();
	return json::undefined;
}

IBrokerControl::PageData PaperTrading::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const IBrokerControl::PageData &pageData) {
	return {};
}

json::Value PaperTrading::getSettings(const std::string_view &pairHint) const {
	std::lock_guard _(lock);
	return {
		json::Object{
			{"name","position"},
			{"label",state.minfo.asset_symbol},
			{"type","number"},
			{"default",state.minfo.leverage?(state.minfo.invert_price?-1:1)*state.collateral.getPos():assets}
		},
		json::Object{
			{"name","currency"},
			{"label",state.minfo.currency_symbol},
			{"type","number"},
			{"default",currency}
		},json::Object {
			{"label","Balances are updated immediately. DO NOT CLICK ON \"Apply settings\" (for next one minute)"},
			{"type","label"}
		}
	};

}

IBrokerControl::BrokerInfo PaperTrading::getBrokerInfo() {
	std::string img;
	auto ptr = dynamic_cast<IBrokerControl *>(state.source.get());
	if (ptr) img = ptr->getBrokerInfo().favicon;
	else img = json::base64->decodeBinaryValue(
			"iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAMAAABg3Am1AAAAM1BMVEVAAAAAAgAXGRYnKSc5OzhG"
			"SEVYWldmaGV2eHWJjImXmZapq6i3urbKzcna3dnq7Ony9fHspJQpAAAAAXRSTlMAQObYZgAAArNJ"
			"REFUSMetVotypDAMq0PIm8T//7UnOcDClm3vZs7D7NLWii3Ldvr19V9N5F98zT796cHZ59Y1yfdw"
			"IqVGuR+c6jbMqtxPsZdSS/Fy/CqU1sdpXb6nKBWAzB/ceTBd59cjoMAkbUN1XM6eCH/3N0BGgCz1"
			"5g7/Cciy+4cXh1gBWOLLv+RXhLYDQqvrARBGSKJn7lXeWYvU1lo46kpAln4C+nzvBR+6Z9Fqq6cQ"
			"2QDtzGkTV+yLAEo3A/ijYhIZwmU9s5DFkhIUWSmdpIban/0gCyMEf0ZAMPo2kagKEuIqzF1Er/nC"
			"eoxYRqzAwYbyq8A/X7tqst4ORI1akuoC1011lRUJ1WuDSsrZpNs7YhPFo2wXyBnAuLZr2+6sl0RA"
			"pGgoK5JpbFtAIwC3AJQOEaKRKB4t4skadGHIiIzDfWIMAOmUpal9ZMveWBfJt5K+sUYCIhCkMHmN"
			"1vMM4N9H0li7QsDYFviueNciZ0nfAZ4hwor0UR3w1RJ0WLhQ30r6dWtYFH9Tx9Ss21nXN83uJDqK"
			"X/lMAdWhpE8BrlPXorZ0SO4sQHoCmHQ+sdf4HDsmzwAPAIcxLUYClPdZ6sYAc7Y97D9bHZn1iU3D"
			"nKWNCw2aZX0C0L9QuhKbzFlqkFmyQyniAyARMKWTvdHR5SNZw9YHEoE5rQEketiHNbkOeBo85GGD"
			"E2CstboJWLm9MHnU/yMJSDd6mADBGHPXsnQPgMwQGH+zvarB6qscvgfWXOI+vzZslzK9ETM+ADwS"
			"QPdfVnKnc29lfb65OMCBe2WmxI+NbSSfLkbrg0Tp5sGZW8bFlD9dpMiJfVB5sM2mj9y5KMQnQCDr"
			"IGSyxGQDAivXFfk9pZKXkIpJwocv8ePlbpfj1XJcfuA8r1+D8FKNXn50PlnDOUX3q++OWHP6i4Pf"
			"/8X4zekPc88zkXyAgKsAAAAASUVORK5CYII="
	).getString();

	return BrokerInfo{
		true,
		"paper_trading",
		"simulator",
		"",
		"1.0.0",
		"",
		img,
		true,
		false
	};
}

IBrokerControl::AllWallets PaperTrading::getWallet() {
	return {};
}

json::Value AbstractPaperTrading::exportState(const TradeState &st) {
	return json::Value(json::object,{
			json::Value("simstate",json::Object{
				{"c", {st.collateral.getPos(),st.collateral.getRPnL(),st.collateral.getOpen()}},
				{"w", saveState(st)}
			})
	});
}

void AbstractPaperTrading::importState(TradeState &st, json::Value v) {
	json::Value state = v["simstate"];
	if (state.defined()) {
		auto c = state["c"];
		st.collateral = ACB(c[2].getNumber(), c[0].getNumber(), c[1].getNumber());
		loadState(st,state["w"]);
	}
}

PaperTrading::TradeState& PaperTrading::getState(const std::string_view &symbol) {
	if (state.pair.empty()) state.src_pair= state.pair = symbol;
	else if (state.pair != symbol) throw std::runtime_error("Market is closed");
	return state;
}


void PaperTrading::loadState(const AbstractPaperTrading::TradeState &st,json::Value state) {
	auto a = state[st.minfo.asset_symbol];
	auto c = state[st.minfo.currency_symbol];
	if (a.defined()) {
		asset_valid = true;
		asset = a.getNumber();
	}
	if (c.defined()) {
		currency_valid = true;
		currency = c.getNumber();
	}
}

void PaperTrading::reset(const std::chrono::system_clock::time_point &tp) {
	std::lock_guard _(lock);
	if (lastReset<tp) {
		if (!state.pair.empty()) {
			simulate(state);
		}
		lastReset = tp;
	}
}

PaperTrading::RawBalance PaperTrading::getRawBalance(const TradeState &st) const {
	return {
		asset,currency
	};
}

json::Value PaperTrading::saveState(const AbstractPaperTrading::TradeState &st) {
	return json::Object {
		{st.minfo.asset_symbol, asset_valid?json::Value(asset):json::Value()},
		{st.minfo.currency_symbol, currency_valid?json::Value(currency):json::Value()}
	};
}
