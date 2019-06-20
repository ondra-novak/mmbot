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

	cfg.acm_factor_buy = ini["acum_factor_buy"].getNumber(1);
	cfg.acm_factor_sell = ini["acum_factor_sell"].getNumber(0);

	cfg.dry_run = force_dry_run?true:ini["dry_run"].getBool(false);
	cfg.internal_balance = cfg.dry_run?true:ini["internal_balance"].getBool(false);
	cfg.detect_manual_trades = ini["detect_manual_trades"].getBool(false);
	cfg.lnspread = ini["lnspread"].getBool(false);

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

	//Load state on first run (if the state is not loaded)
	if (need_load) {
		loadState();
		need_load = false;
	}


	//Get opened orders
	auto orders = getOrders();
	//get current status
	auto status = getMarketStatus();

	if (status.assetBalance < 1e-20) {
		ondra_shared::logFatal("No balance available ($1 $2). Please add some assets or set 'external_assets' to a positive value", status.assetBalance, minfo.asset_symbol);
		return 0;
	}



	//update market fees
	minfo.fees = status.new_fees;
	//process all new trades
	processTrades(status, first_order);
	//merge trades on same price
	mergeTrades(trades.size() - status.new_trades.size());

	double lastTradePrice = trades.empty()?status.curPrice:trades.back().eff_price;



	bool calcadj;
	//only create orders, if there are no trades from previous run
	if (status.new_trades.empty()) {

		ondra_shared::logDebug("internal_balance=$1, external_balance=$2",status.internalBalance,status.assetBalance);
		bool balchange = false;
		if ( !similar(status.internalBalance ,status.assetBalance,1e-5)) {
			ondra_shared::logWarning("Detected balance change: $1 => $2", status.internalBalance, status.assetBalance);
			balchange = true;
		}

		if (!cfg.internal_balance) {
			internal_balance = status.assetBalance-cfg.external_assets;
		}

		//update calculator using current account state
		calcadj = calculator.addTrade(lastTradePrice, status.assetBalance, balchange?-1:0);

		//calculate buy order
		auto buyorder = calculateOrder(-status.curStep*buy_dynmult*cfg.buy_step_mult,
									   status.curPrice, status.assetBalance);
		//calculate sell order
		auto sellorder = calculateOrder(status.curStep*sell_dynmult*cfg.sell_step_mult,
				   	   	   	   	   	   status.curPrice, status.assetBalance);
		//replace order on stockmarket
		replaceIfNotSame(orders.buy, buyorder);
		//replace order on stockmarket
		replaceIfNotSame(orders.sell, sellorder);
		//remember the orders (keep previous orders as well)
		std::swap(lastOrders[0],lastOrders[1]);
		lastOrders[0] = orders;
	} else {
		calcadj = calculator.addTrade(trades.back().eff_price, status.assetBalance, trades.back().eff_size);
	}
	if (calcadj) {
		double c = calculator.balance2price(1.0);
		ondra_shared::logNote("Calculator adjusted: $1 at $2, ref_price=$3 ($4)", calculator.getBalance(), calculator.getPrice(), c, c - prev_calc_ref);
		prev_calc_ref = c;
	}

	//report orders to UI
	statsvc->reportOrders(orders.buy,orders.sell);
	//report trades to UI
	statsvc->reportTrades(trades);
	//report price to UI
	statsvc->reportPrice(status.curPrice);
	//store current price (to build chart)
	chart.push_back(status.chartItem);
	//delete very old data from chart
	if (chart.size() > cfg.spread_calc_mins)
		chart.erase(chart.begin(),chart.end()-cfg.spread_calc_mins);

	//if this was first order, the next will not first order
	first_order = false;

	//update internal balance

	//save state
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
			return false;

		neworder.client_id = magic;
		json::Value placeid;
		if (!orig.has_value()) {
			placeid = stock.placeOrder(cfg.pairsymb, neworder.size, neworder.price,
					neworder.client_id);
		} else if (!orig->isSimilarTo(neworder, minfo.currency_step)) {
			placeid = stock.placeOrder(cfg.pairsymb, neworder.size, neworder.price,
					neworder.client_id, orig->id, std::fabs(orig->size));
			if (placeid == orig->id) return false;
		} else {
			return false;
		}
		if (placeid.isNull() || !placeid.defined()) {
			orig.reset();
			return false;
		} else {
			orig = neworder;
			return true;
		}
	} catch (const std::exception &e) {
		logNote("Order was not placed: ($1 at $2) -  $3", neworder.size, neworder.price, e.what());
		orig.reset();
		return false;
	}

}



MTrader::Status MTrader::getMarketStatus() const {

	Status res;



//	if (!initial_price) initial_price = res.curPrice;

	json::Value lastId;

	if (!trades.empty()) lastId = trades.back().id;
	res.new_trades = stock.getTrades(lastId, cfg.start_time, cfg.pairsymb);

	{
		double balance = 0;
		for (auto &&t:res.new_trades) balance+=t.eff_size;
		res.internalBalance = internal_balance + balance + cfg.external_assets;
	}


	if (cfg.internal_balance) {
		res.assetBalance = res.internalBalance;
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


MTrader::Order MTrader::calculateOrderFeeLess(double step, double curPrice, double balance) const {
	Order order;

	double prevPrice = calculator.balance2price(balance);
	double newPrice = cfg.lnspread?(prevPrice * exp(step)):(prevPrice+step);
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

	double newBalance = calculator.price2balance(newPrice);
	double extra = calculator.calcExtra(prevPrice, newPrice);
	double size = (newBalance - balance)*mult+extra*fact;

	if (size * step > 0) size = 0;
	//fill order
	order.size = size;
	order.price = newPrice;


	return order;

}

MTrader::Order MTrader::calculateOrder(double step, double curPrice, double balance) const {

	Order order(calculateOrderFeeLess(step,curPrice,balance));
	//apply fees
	minfo.addFees(order.size, order.price);

	if (order.size < minfo.min_size && order.size > -minfo.min_size) {
		order.size = 0;
	} else if (minfo.min_volume) {
		double vol = order.size * order.price;
		if (vol < minfo.min_volume && vol > -minfo.min_volume) {
			order.size = 0;
		}
	}
	//order here
	return order;

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
		calculator = Calculator::fromJSON(st["calc"]);
		lastOrders[0] = OrderPair::fromJSON(st["orders"][0]);
		lastOrders[1] = OrderPair::fromJSON(st["orders"][1]);
	}
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
	obj.set("calc", calculator.toJSON());
	obj.set("orders", {lastOrders[0].toJSON(),lastOrders[1].toJSON()});
	storage->store(obj);
}

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


void MTrader::backtest() {
	if (need_load) loadState();
	decltype(trades) data(std::move(trades));
	trades.clear();
	if (data.empty()) return;
	Status st;
	double bal = cfg.external_assets;
	double p = data[0].eff_price;
	double osz = 0;
	for (auto k : data) {
		calculator.addTrade(p,bal,osz);
		double step = k.eff_price - p;
		auto ord = calculateOrder(step,p,bal);
		k.size = k.eff_size = ord.size;
		k.price = k.eff_price = ord.price;
		minfo.removeFees(k.eff_size, k.eff_price);
		k.balance = bal + k.eff_size;
		bal = k.balance;
		p = k.eff_price;
		osz = k.eff_size;
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

void MTrader::processTrades(Status &st,bool first_trade) {

	bool buy_trade = false;
	bool sell_trade = false;

	for (auto &&t : st.new_trades) {
		bool manual = false;
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

		if (!cfg.detect_manual_trades || first_trade)
			manual = false;

		if (!manual) {
			internal_balance += t.eff_size;
		}

		buy_trade = buy_trade || t.eff_size > 0;
		sell_trade = sell_trade || t.eff_size < 0;

		trades.push_back(TWBItem(t, st.assetBalance, manual));
	}
	this->buy_dynmult= raise_fall(this->buy_dynmult, buy_trade);
	this->sell_dynmult= raise_fall(this->sell_dynmult, sell_trade);
	st.internalBalance = internal_balance + cfg.external_assets;
	prev_calc_ref = calculator.balance2price(1);
}
