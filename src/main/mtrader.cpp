#include <imtjson/value.h>
#include <imtjson/string.h>
#include "istockapi.h"
#include "mtrader.h"

#include <chrono>
#include <cmath>
#include <shared/logOutput.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
#include <numeric>

#include "emulator.h"
#include "sgn.h"

using ondra_shared::logNote;


MTrader::MTrader(IStockSelector &stock_selector,
		StoragePtr &&storage,
		PStatSvc &&statsvc,
		Config config)
:stock(selectStock(stock_selector,config,ownedStock))
,cfg(std::move(config))
,minfo(stock.getMarketInfo(cfg.pairsymb))
,storage(std::move(storage))
,statsvc(std::move(statsvc))
{
	this->statsvc->setInfo(cfg.title, minfo.asset_symbol, minfo.currency_symbol, stock.isTest());


}



MTrader::Config MTrader::load(const ondra_shared::IniConfig::Section& ini, bool force_dry_run) {
	Config cfg;


	cfg.broker = ini.mandatory["broker"].getString();
	cfg.spread_calc_mins = ini["spread_calc_hours"].getUInt(24*5)*60;
	cfg.spread_calc_min_trades = ini["spread_calc_min_trades"].getUInt(8);
	cfg.spread_calc_max_trades = ini["spread_calc_max_trades"].getUInt(24);
	cfg.pairsymb = ini.mandatory["pair_symbol"].getString();

	cfg.buy_mult = ini["buy_mult"].getNumber(1.0);
	cfg.sell_mult = ini["sell_mult"].getNumber(1.0);
	cfg.buy_step_mult = ini["buy_step_mult"].getNumber(1.0);
	cfg.sell_step_mult = ini["sell_step_mult"].getNumber(1.0);
	cfg.external_assets = ini["external_assets"].getNumber(0);

	cfg.acm_factor_buy = ini["acm_factor_buy"].getNumber(0);
	cfg.acm_factor_sell = ini["acm_factor_sell"].getNumber(1);

	cfg.dry_run = force_dry_run?true:ini["dry_run"].getBool(false);
	cfg.internal_balance = cfg.dry_run?true:ini["internal_balance"].getBool(false);
	cfg.detect_manual_trades = cfg.dry_run?true:ini["detect_manual_trades"].getBool(false);

	cfg.dynmult_raise = ini["dynmult_raise"].getNumber(200);
	cfg.dynmult_fall = ini["dynmult_fall"].getNumber(1);

	cfg.title = ini["title"].getString();


	cfg.start_time = ini["start_time"].getUInt(0);
	return cfg;
}

bool MTrader::Order::isSimilarTo(const Order& other, double step) {
	return std::fabs(price - other.price) < step && size * other.size > 0;
}


IStockApi &MTrader::selectStock(IStockSelector &stock_selector, const Config &conf,	std::unique_ptr<IStockApi> &ownedStock) {
	IStockApi *s = stock_selector.getStock(conf.broker);
	if (s == nullptr) throw std::runtime_error(std::string("Unknown stock market name: ")+std::string(conf.broker));
	if (conf.dry_run) {
		ownedStock = std::make_unique<EmulatorAPI>(*s);
		return *ownedStock;
	} else {
		return *s;
	}
}

double MTrader::raise_fall(double v, bool raise) const {
	if (raise) {
		double rr = (1.0+cfg.dynmult_raise/100.0);
		return v * rr;
	} else {
		double ff = (1.0-cfg.dynmult_fall/100.0);
		return std::max(1.0,v * ff);
	}
}

int MTrader::perform() {

	if (need_load) {
		loadState();
		need_load = false;
	}


	auto orders = getOrders();
	auto status = getMarketStatus();
	minfo.fees = status.new_fees;
	BalanceState lastTrade = processTrades(status, orders.buy.has_value() && orders.sell.has_value(), first_order);
	first_order = false;
	mergeTrades(trades.size() - status.new_trades.size());

	if (status.curStep > status.curPrice*1e-10) {
		auto buyorder = calculateOrder(-status.curStep*buy_dynmult*cfg.buy_step_mult,
									   status.curPrice, lastTrade);
		auto sellorder = calculateOrder(status.curStep*sell_dynmult*cfg.sell_step_mult,
				   	   	   	   	   	   status.curPrice, lastTrade);
		replaceIfNotSame(orders.buy, buyorder);
		replaceIfNotSame(orders.sell, sellorder);
	}
	statsvc->reportOrders(orders.buy,orders.sell);
	std::swap(lastOrders[0],lastOrders[1]);
	lastOrders[0] = orders;

	statsvc->reportTrades(trades);
	statsvc->reportPrice(status.curPrice);

	chart.push_back(status.chartItem);
	if (chart.size() > cfg.spread_calc_mins)
		chart.erase(chart.begin(),chart.end()-cfg.spread_calc_mins);

	saveState();

	return 0;
}

static std::uintptr_t magic = 0xFEEDBABE;

MTrader::OrderPair MTrader::getOrders() {
	OrderPair ret;
	auto data = stock.getOpenOrders(cfg.pairsymb);
	for (auto &&x: data) {
		if (x.client_id == magic) {
			Order o(x);
			if (o.size<0) {
				if (ret.sell.has_value()) {
					ondra_shared::logWarning("Multiple sell orders");
				}
				ret.sell = o;
			} else {
				if (ret.buy.has_value()) {
					ondra_shared::logWarning("Multiple buy orders");
				}
				ret.buy = o;
			}
		}
	}
	return ret;
}

bool MTrader::replaceIfNotSame(std::optional<Order>& orig, Order neworder) {

	try {
		if (neworder.price < 0)
			throw std::runtime_error("Negative price - rejected");
		if (neworder.size == 0)
			throw std::runtime_error("Zero size - rejected");
		neworder.client_id = magic;
		bool res = false;
		if (!orig.has_value()) {
			neworder.id = stock.placeOrder(cfg.pairsymb, neworder );
			res = true;
		} else if (!orig->isSimilarTo(neworder, minfo.currency_step)) {
			neworder.id = orig->id;
			neworder.id = stock.placeOrder(cfg.pairsymb, neworder);
		} else {
			return false;
		}
		orig = neworder;
		return res;
	} catch (const std::exception &e) {
		logNote("Order was not placed: ($1 at $2) -  $3", neworder.size, neworder.price, e.what());
		orig.reset();
		return false;
	}

}
double MTrader::addFees(double price, double dir) const {
	return price*(1 - minfo.fees*dir);
}

double MTrader::removeFees(double price, double dir) const {
	return price/(1 - minfo.fees*dir);
}


MTrader::Status MTrader::getMarketStatus() const {

	Status res;



//	if (!initial_price) initial_price = res.curPrice;

	json::Value lastId;

	if (!trades.empty()) lastId = trades.back().id;
	res.new_trades = stock.getTrades(lastId, cfg.start_time, cfg.pairsymb);


	if (cfg.internal_balance) {
		double balance = 0;
		for (auto &&t:res.new_trades) balance+=t.eff_size;
		res.assetBalance = internal_balance + balance + cfg.external_assets;
	} else{
		res.assetBalance = stock.getBalance(minfo.asset_symbol)+ cfg.external_assets;
	}



	auto step = statsvc->calcSpread(chart,cfg,minfo,res.assetBalance,prev_spread);
	res.curStep = step;
	prev_spread = step;


	res.new_fees = stock.getFees(cfg.pairsymb);

	auto ticker = stock.getTicker(cfg.pairsymb);
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);

	res.chartItem.time = ticker.time;
	res.chartItem.bid = ticker.bid;
	res.chartItem.ask = ticker.ask;

	return res;
}


MTrader::Order MTrader::calculateOrderFeeLess(double step,
		double curPrice, const BalanceState &lastTrade) const {
	Order order;
	// calculate new price for the order
	double newPrice = lastTrade.price + step;
	double fact;
	double mult;

	if (step < 0) {
		//if price is lower than old, check whether current price is above
		//otherwise lower the price more
		if (newPrice > curPrice) newPrice = curPrice;
		fact = cfg.acm_factor_buy;
		mult = cfg.buy_mult;
	} else {
		//if price is higher then old, check whether current price is below
		//otherwise highter the newPrice more

		if (newPrice < curPrice) newPrice = curPrice;
		fact = cfg.acm_factor_sell;
		mult = cfg.sell_mult;
	}

	// calculate new assets for the new price
	double newAssets = lastTrade.balance * sqrt(lastTrade.price/newPrice) * mult;
	// express the difference
	double size = newAssets - lastTrade.balance;
	// calculate extra money available after this trade
	double extra = (lastTrade.price*lastTrade.balance-newPrice*size-lastTrade.balance*sqrt(lastTrade.price*newPrice))/newPrice;
	// use that extra money to additional trade specified as factor in the config
	double extra_c = extra*fact;

	//fill order
	order.size = size+extra_c;
	order.price = newPrice;

	return order;

}

MTrader::Order MTrader::calculateOrder(double step,
		double curPrice, const BalanceState &lastTrade) const {

	Order order(calculateOrderFeeLess(step,curPrice,lastTrade));
	//apply fees
	minfo.addFees(order.size, order.price);
	//order here
	return order;

}


/*

MTrader::Order MTrader::calculateSellOrder(const Status& status, double dynmult, const std::optional<Order> &curOrder) const {

	Order order;
	double curPrice = status.basePrice;
	double step = status.curStep*cfg.buy_step_mult*dynmult;

	double newPrice = basePrice + step;
	if (newPrice < status.curPrice) {
		if (curOrder.has_value()) return *curOrder;
		else newPrice = status.curPrice;
	}
	double assets = status.assetBalance*cfg.buy_mult + cfg.asset_base;
	double newAssets = assets*0.5*( basePrice/newPrice - 1.0);

	minfo.addFees(newAssets, newPrice);

	order.size = newAssets;
	order.price = newPrice;
	return order;
}

MTrader::Order MTrader::calculateBuyOrder(const Status& status, double dynmult, const std::optional<Order> &curOrder) const {

	Order order;
	double basePrice = status.basePrice;
	double step = status.curStep*cfg.buy_step_mult*dynmult;

	double newPrice = basePrice - step;
	if (newPrice > status.curPrice) {
		if (curOrder.has_value()) return *curOrder;
		else newPrice = status.curPrice-minfo.currency_step*2;
	}

	double assets = status.assetBalance*cfg.buy_mult + cfg.asset_base;
	double newAssets = assets*0.5*(basePrice/newPrice - 1.0);

	minfo.addFees(newAssets, newPrice);

	order.size = newAssets;
	order.price = newPrice;
	return order;
}
*/

double MTrader::adjValue(double sz, double step) {
	return std::round(sz/step)*step;
}
double MTrader::adjValueCeil(double sz, double step) {
	return std::ceil(sz/step)*step;
}

void MTrader::loadState() {
	if (storage == nullptr) return;
	auto st = storage->load();
	need_load = false;

	bool wastest = false;

	auto curtest = stock.isTest();

	bool recalc_trades = false;

	if (st.defined()) {
		json::Value tst = st["testStartTime"];
		wastest = tst.defined();
		testStartTime = tst.getUInt();
		auto state = st["state"];
		if (state.defined()) {
			if (!curtest) {
				buy_dynmult = state["buy_dynmult"].getNumber();
				sell_dynmult = state["sell_dynmult"].getNumber();
			}
			prev_spread = state["spread"].getNumber();
			internal_balance = state["internal_balance"].getNumber();
		}
		auto chartSect = st["chart"];
		if (chartSect.defined()) {
			chart.clear();
			for (json::Value v: chartSect) {
				chart.push_back({
					v["time"].getUInt(),
					v["ask"].getNumber(),
					v["bid"].getNumber()
				});
			}
		}
		{
			auto trSect = st["trades"];
			if (trSect.defined()) {
				trades.clear();
				for (json::Value v: trSect) {
					TWBItem itm = TWBItem::fromJSON(v);
					if (wastest && !curtest && itm.time > testStartTime ) {
						continue;
					} else {
						trades.push_back(itm);
						recalc_trades = recalc_trades || itm.balance == TWBItem::no_balance;
					}
				}
			}
			mergeTrades(0);
		}
	}
	lastOrders[0] = OrderPair::fromJSON(st["orders"][0]);
	lastOrders[1] = OrderPair::fromJSON(st["orders"][1]);
	if (curtest && testStartTime == 0) {
		testStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
				).count();
	}
	if (recalc_trades) {
			double endBal = stock.getBalance(minfo.asset_symbol) + cfg.external_assets;
		double chng = std::accumulate(trades.begin(), trades.end(),0.0,[](auto &&a, auto &&b) {
			return a + b.eff_size;
		});
		double begBal = endBal-chng;
		for (auto &&t:trades) {
			if (t.balance < 0) t.balance = begBal+t.eff_size;
			begBal = t.balance;
		}
	}
	if (internal_balance == 0 && cfg.internal_balance) {
		if (!trades.empty()) internal_balance = trades.back().balance- cfg.external_assets;
		else internal_balance = stock.getBalance(minfo.asset_symbol);
	}
}

void MTrader::saveState() {
	if (storage == nullptr) return;
	json::Object obj;

	obj.set("version",2);
	if (stock.isTest()) {
		obj.set("testStartTime", testStartTime);;
	}

	{
		auto st = obj.object("state");
		st.set("buy_dynmult", buy_dynmult);
		st.set("sell_dynmult", sell_dynmult);
		st.set("spread", prev_spread);
		st.set("internal_balance", internal_balance);
	}
	{
		auto ch = obj.array("chart");
		for (auto &&itm: chart) {
			ch.push_back(json::Object("time", itm.time)
				  ("ask",itm.ask)
				  ("bid",itm.bid));
		}
	}
	{
		auto tr = obj.array("trades");
		for (auto &&itm:trades) {
			tr.push_back(itm.toJSON());
		}
	}
	obj.set("orders", {lastOrders[0].toJSON(),lastOrders[1].toJSON()});
	storage->store(obj);
}

/*
double MTrader::range_max_price(Status st, double &avail_assets) {
	std::optional<Order> empty;
	double budget = avail_assets;
	double steppart = st.curStep/st.basePrice;
	Order o = calculateSellOrder(st, 1.0, empty);
	while (budget+o.size > minfo.min_size) {
		budget += o.size;
		st.assetBalance += o.size;
		ondra_shared::logDebug("Sell $1 at $2 - remaining: $3 $4", o.size, o.price, budget, minfo.asset_symbol);
		st.basePrice = removeFees(o.price, -1);
		st.curStep = st.basePrice*steppart;
		o = calculateSellOrder(st, 1.0, empty);
	}
	avail_assets = budget;
	return st.basePrice;

}
double MTrader::range_min_price(Status st, double &avail_money) {
	std::optional<Order> empty;
	double budget = avail_money;
	double min_price = st.basePrice*0.00001;
	double steppart = st.curStep/st.basePrice;
	Order o = calculateBuyOrder(st, 1.0, empty);
	Order p = o;
	while (budget > 0  && o.price>min_price) {
		st.basePrice = removeFees(o.price, +1);
		st.curStep = st.basePrice*steppart;
		st.assetBalance += o.size;
		budget -= o.size*st.basePrice;
		ondra_shared::logDebug("Buy $1 at $2 - remaining: $3 $4", o.size, o.price, budget, minfo.currency_symbol);
		o = calculateBuyOrder(st, 1.0, empty);
		if (fabs(o.price - p.price) < (o.price+p.price)*1e-8) {
			steppart=steppart*2;
		}
		p = o;
	}
	avail_money = budget;
	return st.basePrice;

}
*/
MTrader::CalcRes MTrader::calc_min_max_range() {

	CalcRes res {};
	loadState();


	res.avail_assets = stock.getBalance(minfo.asset_symbol);
	res.avail_money = stock.getBalance(minfo.currency_symbol);
	res.cur_price = stock.getTicker(cfg.pairsymb).last;
	res.assets = res.avail_assets+cfg.external_assets;
	res.value = res.assets * res.cur_price;
	res.max_price = pow2((res.assets * sqrt(res.cur_price))/(res.assets -res.avail_assets));
	double S = res.value - res.avail_money;
	res.min_price = S<=0?0:pow2(S/(res.assets*sqrt(res.cur_price)));
	return res;


}

void MTrader::mergeTrades(std::size_t fromPos) {
	if (fromPos) --fromPos;
	auto wr = trades.begin()+fromPos;
	auto rd = wr;
	auto end = trades.end();

	if (rd == end) return ;
	++rd;
	while (rd != end) {
		if (rd->price == wr->price && rd->size * wr->size > 0) {
			wr->size+=rd->size;
			wr->balance = rd->balance;
			wr->eff_price = rd->eff_price;
			wr->eff_size+=rd->eff_size;
			wr->time = rd->time;
			wr->id = rd->id;
		} else {
			++wr;
			if (wr != rd) *wr = *rd;
		}
		++rd;
	}
	++wr;
	if (wr != rd) trades.erase(wr,end);
}



bool MTrader::eraseTrade(std::string_view id, bool trunc) {
	if (need_load) loadState();
	auto iter = std::find_if(trades.begin(), trades.end(), [&](const IStockApi::Trade &tr) {
		json::String s = tr.id.toString();
		return s.str() == id;
	});
	if (iter == trades.end()) return false;
	if (trunc) {
		trades.erase(iter, trades.end());
	} else {
		trades.erase(iter);
	}
	saveState();
	return true;
}

MTrader::BalanceState MTrader::getLastTrade(const Status& st) {
	auto iter = trades.rbegin();
	if (last_trade_partial) ++iter;
	while (iter != trades.rend()) {
		if (!iter->manual_trade) return {iter->eff_price, st.assetBalance};
		++iter;
	}
	if (trades.empty()) return {st.curPrice, st.assetBalance};
	else return {trades.back().eff_price, st.assetBalance};
}

void MTrader::backtest() {
	if (need_load) loadState();
	decltype(trades) data(std::move(trades));
	trades.clear();
	Status st;
	double bal = cfg.external_assets;
	for (auto k : data) {
		st.curPrice = k.eff_price;
		st.assetBalance = bal;
		auto lastT = getLastTrade(st);
		double step = k.eff_price - lastT.price;
		auto ord = calculateOrder(step,lastT.price,lastT);
		k.size = k.eff_size = ord.size;
		k.price = k.eff_price = ord.price;
		minfo.removeFees(k.eff_size, k.eff_price);
		k.balance = bal + k.eff_size;
		bal = k.balance;
		trades.push_back(k);
	}
	saveState();
}

MTrader::OrderPair MTrader::OrderPair::fromJSON(json::Value json) {
	json::Value bj = json["bj"];
	json::Value sj = json["sj"];
	std::optional<Order> nullorder;
	return 	OrderPair {
		bj.defined()?std::optional<Order>(Order::fromJSON(bj)):nullorder,
		sj.defined()?std::optional<Order>(Order::fromJSON(sj)):nullorder
	};
}

json::Value MTrader::OrderPair::toJSON() const {
	return json::Object
			("buy",buy.has_value()?buy->toJSON():json::Value())
			("sell",sell.has_value()?sell->toJSON():json::Value());
}

MTrader::BalanceState MTrader::processTrades( const Status &st, bool partial_execution, bool first_trade) {
	BalanceState bs = getLastTrade(st);

	bool buy_trade = false;
	bool sell_trade = false;

	for (auto &&t : st.new_trades) {
		bool manual = false;
		if (cfg.detect_manual_trades && !first_trade) {
			manual = true;
			Order fkord(t.size, t.price);
			for (auto &lo : lastOrders) {
				manual = manual && !((lo.buy.has_value() && lo.buy->isSimilarTo(fkord, minfo.currency_step))
						|| (lo.sell.has_value() && lo.sell->isSimilarTo(fkord, minfo.currency_step)));
			}
			if (manual) {
				for (auto &lo : lastOrders) {
					ondra_shared::logNote("Detected manual trade: $1 $2 $3",
							!lo.buy.has_value()?0.0:lo.buy->price, fkord.price, !lo.sell.has_value()?0.0:lo.sell->price);
				}
			}
		}
		if (partial_execution)
			ondra_shared::logNote("Detected partial execution");

		if (!manual && !partial_execution) bs.price = t.eff_price;
		if (cfg.internal_balance) {
			if (!manual) {
				internal_balance += t.eff_size;
			}
		} else {
			internal_balance = st.assetBalance-cfg.external_assets;
		}
		buy_trade = buy_trade || t.eff_size > 0;
		sell_trade = sell_trade || t.eff_size < 0;

		trades.push_back(TWBItem(t, st.assetBalance, manual));
	}
	this->buy_dynmult= raise_fall(this->buy_dynmult, buy_trade);
	this->sell_dynmult= raise_fall(this->sell_dynmult, sell_trade);
	last_trade_partial = partial_execution;
	return bs;
}
