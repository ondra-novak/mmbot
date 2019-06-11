#include <imtjson/value.h>
#include <imtjson/string.h>
#include "istockapi.h"
#include "mtrader.h"

#include <chrono>
#include <cmath>
#include <shared/logOutput.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
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
,statsvc(std::move(statsvc)) {
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
	cfg.asset_base = ini["external_assets"].getNumber(0);

	cfg.dry_run = force_dry_run?true:ini["dry_run"].getBool(false);

	cfg.dynmult_raise = ini["dynmult_raise"].getNumber(200);
	cfg.dynmult_fall = ini["dynmult_fall"].getNumber(1);

	cfg.title = ini["title"].getString();


	cfg.start_time = ini["start_time"].getUInt(0);
	return cfg;
}

bool MTrader::Order::isSimilarTo(const Order& other) {
	return std::fabs(price - other.price) < fabs((price+other.price)/1e8) && size * other.size > 0;
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

	auto status = getMarketStatus();
	auto orders = getOrders();
	buy_dynmult = raise_fall(buy_dynmult, !orders.buy.has_value() && !first_order);
	sell_dynmult = raise_fall(sell_dynmult, !orders.sell.has_value() && !first_order);
	first_order = false;
	minfo.fees = status.new_fees;
	trades.insert(trades.end(),status.new_trades.begin(), status.new_trades.end());
	mergeTrades(trades.size() - status.new_trades.size());

	if (status.curStep > status.curPrice*1e-10) {
		auto buyorder = calculateBuyOrder(status,buy_dynmult,orders.buy);
		auto sellorder = calculateSellOrder(status,sell_dynmult,orders.sell);
		replaceIfNotSame(orders.buy, buyorder);
		replaceIfNotSame(orders.sell, sellorder);
	}
	statsvc->reportOrders(orders.buy,orders.sell);
	if (!orders.buy.has_value()) buy_dynmult = 1;
	if (!orders.sell.has_value()) sell_dynmult = 1;

	statsvc->reportTrades(status.assetBalance+cfg.asset_base, trades);
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
				ret.sell = o;
			} else {
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
		} else if (!orig->isSimilarTo(neworder)) {
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

	auto balance = stock.getBalance(minfo.asset_symbol);
	res.assetBalance = balance;

	auto ticker = stock.getTicker(cfg.pairsymb);
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);

	if (!initial_price) initial_price = res.curPrice;

	json::Value lastId;

	if (!trades.empty()) lastId = trades.back().id;
	res.new_trades = stock.getTrades(lastId, cfg.start_time, cfg.pairsymb);

	const IStockApi::TradeHistory &lasttr = res.new_trades.empty()?trades:res.new_trades;
	if (lasttr.empty()) res.lastTradePrice = res.basePrice = initial_price;
	else {
		auto &t = lasttr.back();
		res.lastTradePrice = t.price;
		res.basePrice = t.eff_price;
	}


	auto step = statsvc->calcSpread(chart,cfg,minfo,res.assetBalance+cfg.asset_base,prev_spread);
	res.curStep = step;
	prev_spread = step;



	res.chartItem.time = ticker.time;
	res.chartItem.bid = ticker.bid;
	res.chartItem.ask = ticker.ask;

	res.new_fees = stock.getFees(cfg.pairsymb);

	return res;
}



MTrader::Order MTrader::calculateSellOrder(const Status& status, double dynmult, const std::optional<Order> &curOrder) const {

	Order order;
	double basePrice = status.basePrice;
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
					TradeItem itm = TradeItem::fromJSON(v);
					if (wastest && !curtest && itm.time > testStartTime ) {
						continue;
					} else {
						trades.push_back(itm);
					}
				}
			}
			mergeTrades(0);
		}
	}
	if (curtest && testStartTime == 0) {
		testStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
				).count();
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
	storage->store(obj);
}


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

MTrader::CalcRes MTrader::calc_min_max_range() {

	CalcRes res {};
	loadState();
	Status st = getMarketStatus();
	res.avail_assets = stock.getBalance(minfo.asset_symbol);
	res.avail_money = stock.getBalance(minfo.currency_symbol);
	res.cur_price = stock.getTicker(cfg.pairsymb).last;
	res.assets = res.avail_assets+cfg.asset_base;
	res.value = res.assets * res.cur_price;
	res.money_left = res.avail_money;
	res.assets_left = res.avail_assets;
	if (st.assetBalance > 1e-20) {
		res.min_price = range_min_price(st, res.money_left);
		res.max_price = range_max_price(st, res.assets_left);
	}
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
