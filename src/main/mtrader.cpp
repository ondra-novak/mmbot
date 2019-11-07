#include <imtjson/value.h>
#include <imtjson/string.h>
#include "istockapi.h"
#include "mtrader.h"
#include "strategy.h"

#include <chrono>
#include <cmath>
#include <shared/logOutput.h>
#include <imtjson/object.h>
#include <imtjson/array.h>
#include <numeric>
#include <queue>
#include <random>

#include "../shared/stringview.h"
#include "emulator.h"
#include "ibrokercontrol.h"
#include "sgn.h"

using ondra_shared::logDebug;
using ondra_shared::logInfo;
using ondra_shared::logNote;
using ondra_shared::StringView;
using ondra_shared::StrViewA;

json::NamedEnum<Dynmult_mode> strDynmult_mode  ({
	{Dynmult_mode::independent, "independent"},
	{Dynmult_mode::together, "together"},
	{Dynmult_mode::alternate, "alternate"},
	{Dynmult_mode::half_alternate, "half_alternate"}
});

std::string_view MTrader::vtradePrefix = "__vt__";


static double default_value(json::Value data, double defval) {
	if (data.type() == json::number) return data.getNumber();
	else return defval;
}
static StrViewA default_value(json::Value data, StrViewA defval) {
	if (data.type() == json::string) return data.getString();
	else return defval;
}
/*static intptr_t default_value(json::Value data, intptr_t defval) {
	if (data.type() == json::number) return data.getInt();
	else return defval;
}*/
static uintptr_t default_value(json::Value data, uintptr_t defval) {
	if (data.type() == json::number) return data.getUInt();
	else return defval;
}
static bool default_value(json::Value data, bool defval) {
	if (data.type() == json::boolean) return data.getBool();
	else return defval;
}


void MTrader_Config::loadConfig(json::Value data, bool force_dry_run) {
	pairsymb = data["pair_symbol"].getString();
	broker = data["broker"].getString();
	title = data["title"].getString();

	auto strdata = data["strategy"];
	auto strstr = strdata["type"].toString();
	strategy = Strategy::create(strstr.str(), strdata);


	buy_mult = default_value(data["buy_mult"],1.0);
	sell_mult = default_value(data["sell_mult"],1.0);
	buy_step_mult = default_value(data["buy_step_mult"],1.0);
	sell_step_mult = default_value(data["sell_step_mult"],1.0);
	min_size = default_value(data["min_size"],0.0);
	max_size = default_value(data["max_size"],0.0);

	dynmult_raise = default_value(data["dynmult_raise"],0.0);
	dynmult_fall = default_value(data["dynmult_fall"],0.0);
	dynmult_mode = strDynmult_mode[default_value(data["dynmult_mode"], StrViewA("half_alternate"))];

	accept_loss = default_value(data["accept_loss"], static_cast<uintptr_t>(1));

	force_spread = default_value(data["force_spread"], 0.0);
	report_position_offset = default_value(data["report_position_offset"], 0.0);

	spread_calc_sma_hours = default_value(data["spread_calc_sma_hours"], static_cast<uintptr_t>(2))*60;
	spread_calc_stdev_hours = default_value(data["spread_calc_stdev_hours"], static_cast<uintptr_t>(8))*60;

	dry_run = force_dry_run || default_value(data["dry_run"], false);
	internal_balance = default_value(data["internal_balance"], false);
	detect_manual_trades= default_value(data["detect_manual_trades"], false);
	enabled= default_value(data["enabled"], true);
	dust_orders= default_value(data["dust_orders"], true);
	dynmult_scale = default_value(data["dynmult_scale"], true);

	if (dynmult_raise > 1e6) throw std::runtime_error("'dynmult_raise' is too big");
	if (dynmult_raise < 0) throw std::runtime_error("'dynmult_raise' is too small");
	if (dynmult_fall > 100) throw std::runtime_error("'dynmult_fall' must be below 100");
	if (dynmult_fall <= 0) throw std::runtime_error("'dynmult_fall' must not be negative or zero");

}

MTrader::MTrader(IStockSelector &stock_selector,
		StoragePtr &&storage,
		PStatSvc &&statsvc,
		Config config)
:stock(selectStock(stock_selector,config,ownedStock))
,cfg(config)
,storage(std::move(storage))
,statsvc(std::move(statsvc))
,strategy(config.strategy)
{
	//probe that broker is valid configured
	stock.testBroker();
	magic = this->statsvc->getHash() & 0xFFFFFFFF;
	std::random_device rnd;
	uid = 0;
	while (!uid) {
		uid = rnd();
	}

}


bool MTrader::Order::isSimilarTo(const Order& other, double step) {
	return std::fabs(price - other.price) < step && size * other.size > 0;
}


IStockApi &MTrader::selectStock(IStockSelector &stock_selector, const Config &conf,	std::unique_ptr<IStockApi> &ownedStock) {
	IStockApi *s = stock_selector.getStock(conf.broker);
	if (s == nullptr) throw std::runtime_error(std::string("Unknown stock market name: ")+std::string(conf.broker));
	if (conf.dry_run) {
		ownedStock = std::make_unique<EmulatorAPI>(*s, 0);
		return *ownedStock;
	} else {
		return *s;
	}
}

double MTrader::raise_fall(double v, bool raise) const {
	if (raise) {
		double rr = cfg.dynmult_raise/100.0;
		return v + rr;
	} else {
		double ff = cfg.dynmult_fall/100.0;
		return std::max(1.0,v - ff);
	}
}
/*
static auto calc_margin_range(double A, double D, double P) {
	double x1 = (A*P - 2*sqrt(A*D*P) + D)/A;
	double x2 = (A*P + 2*sqrt(A*D*P) + D)/A;
	return std::make_pair(x1,x2);
}
*/

void MTrader::init() {
	if (need_load){
		loadState();
		need_load = false;
	}
}

const MTrader::TradeHistory& MTrader::getTrades() const {
	return trades;
}

void MTrader::perform(bool manually) {

	try {
		init();


		//Get opened orders
		auto orders = getOrders();
		//get current status
		auto status = getMarketStatus();

		std::string buy_order_error;
		std::string sell_order_error;

		//update market fees
		minfo.fees = status.new_fees;
		//process all new trades
		processTrades(status);
		//merge trades on same price
		mergeTrades(trades.size() - status.new_trades.trades.size());

		double lastTradePrice = !trades.empty()?trades.back().eff_price:strategy.isValid()?strategy.getEquilibrium():status.curPrice;
		double lastTradeSize = trades.empty()?0:trades.back().eff_size;



		//only create orders, if there are no trades from previous run
		if (status.new_trades.trades.empty()) {


			if (recalc) {
				update_dynmult(lastTradeSize > 0, lastTradeSize < 0);
			}

			if (!strategy.isValid()) {
				strategy.init(status.curPrice, status.assetBalance, status.currencyBalance);
			}


			ondra_shared::logDebug("internal_balance=$1, external_balance=$2",status.internalBalance,status.assetBalance);

			//calculate buy order
			auto buyorder = calculateOrder(lastTradePrice,
										  -status.curStep*cfg.buy_step_mult, buy_dynmult,
										   status.ticker.bid, status.assetBalance,
										   cfg.buy_mult);
			//calculate sell order
			auto sellorder = calculateOrder(lastTradePrice,
										   status.curStep*cfg.sell_step_mult, sell_dynmult,
										   status.ticker.ask, status.assetBalance,
										   cfg.sell_mult);

			if (!cfg.enabled)  {
				if (orders.buy.has_value())
					stock.placeOrder(cfg.pairsymb,0,0,magic,orders.buy->id,0);
				if (orders.sell.has_value())
					stock.placeOrder(cfg.pairsymb,0,0,magic,orders.sell->id,0);
				statsvc->reportError(IStatSvc::ErrorObj("Automatic trading is disabled"));
			} else {
				try {
					setOrder(orders.buy, buyorder);
				} catch (std::exception &e) {
					buy_order_error = e.what();
					if (!acceptLoss(orders.buy, buyorder, status)) {
						orders.buy = buyorder;
					}
				}

				try {
					setOrder(orders.sell, sellorder);
				} catch (std::exception &e) {
					sell_order_error = e.what();
					if (!acceptLoss(orders.sell, sellorder, status)) {
						orders.sell = sellorder;
					}
				}
				if (!recalc && !manually) {
					update_dynmult(false,false);
				}

				//report order errors to UI
				statsvc->reportError(IStatSvc::ErrorObj(buy_order_error, sell_order_error));

			}

			recalc = false;

		} else {


			recalc = true;
		}

		//report orders to UI
		statsvc->reportOrders(orders.buy,orders.sell);
		//report trades to UI
		statsvc->reportTrades(trades);
		//report price to UI
		statsvc->reportPrice(status.curPrice);
		//report misc
		{
			auto minmax = strategy.calcSafeRange(status.assetBalance, status.currencyBalance);

			statsvc->reportMisc(IStatSvc::MiscData{
				status.new_trades.trades.empty()?0:sgn(status.new_trades.trades.back().size),
				strategy.getEquilibrium(),
				status.curPrice * (exp(status.curStep) - 1),
				buy_dynmult,
				sell_dynmult,
				minmax.min,
				minmax.max,
				trades.size(),
				trades.empty()?0:(trades.back().time-trades[0].time)
			});

		}

		if (!manually) {
			//store current price (to build chart)
			chart.push_back(status.chartItem);
			{
				//delete very old data from chart
				unsigned int max_count = std::max(cfg.spread_calc_sma_hours, cfg.spread_calc_stdev_hours);
				if (chart.size() > max_count)
					chart.erase(chart.begin(),chart.end()-max_count);
			}
		}

		internal_balance = status.internalBalance;
		currency_balance_cache = status.currencyBalance;
		lastTradeId  = status.new_trades.lastId;



		//save state
		saveState();

	} catch (std::exception &e) {
		statsvc->reportError(IStatSvc::ErrorObj(e.what()));
		throw;
	}
}


MTrader::OrderPair MTrader::getOrders() {
	OrderPair ret;
	auto data = stock.getOpenOrders(cfg.pairsymb);
	for (auto &&x: data) {
		try {
			if (x.client_id == magic) {
				Order o(x);
				if (o.size<0) {
					if (ret.sell.has_value()) {
						ondra_shared::logWarning("Multiple sell orders (trying to cancel)");
						stock.placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.sell = o;
					}
				} else {
					if (ret.buy.has_value()) {
						ondra_shared::logWarning("Multiple buy orders (trying to cancel)");
						stock.placeOrder(cfg.pairsymb,0,0,json::Value(),x.id);
					} else {
						ret.buy = o;
					}
				}
			}
		} catch (std::exception &e) {
			ondra_shared::logError("$1", e.what());
		}
	}
	return ret;
}


void MTrader::setOrder(std::optional<Order> &orig, Order neworder) {
	try {
		if (neworder.price < 0 || neworder.size == 0) return;
		neworder.client_id = magic;
		json::Value replaceid;
		double replaceSize = 0;
		if (orig.has_value()) {
			if (orig->isSimilarTo(neworder, minfo.currency_step)) return;
			replaceid = orig->id;
			replaceSize = std::fabs(orig->size);
		}
		json::Value placeid = stock.placeOrder(
					cfg.pairsymb,
					neworder.size,
					neworder.price,
					neworder.client_id,
					replaceid,
					replaceSize);
		if (placeid.isNull() || !placeid.defined()) {
			orig.reset();
		} else if (placeid != replaceid) {
			orig = neworder;
		}
	} catch (...) {
		orig.reset();
		throw;
	}
}



json::Value MTrader::getTradeLastId() const{
	json::Value res;
	if (!trades.empty()) {
		auto i = std::find_if_not(trades.rbegin(), trades.rend(), [&](auto &&x) {
			return json::StrViewA(x.id.toString()).begins(vtradePrefix);
		});
		if (i != trades.rend()) res = i->id;
	}
	return res;
}

MTrader::Status MTrader::getMarketStatus() const {

	Status res;



//	if (!initial_price) initial_price = res.curPrice;

	json::Value lastId;

	if (!trades.empty()) lastId = getTradeLastId();

	res.new_trades = stock.syncTrades(lastTradeId, cfg.pairsymb);

	{
		res.internalBalance = std::accumulate(res.new_trades.trades.begin(),
				res.new_trades.trades.end(),0.0,
				[&](auto &&a, auto &&b) {return a + b.eff_size;});
		if (internal_balance.has_value()) res.internalBalance += *internal_balance;
	}


	if (cfg.internal_balance) {
		res.assetBalance = res.internalBalance;
	} else{
		res.assetBalance = stock.getBalance(minfo.asset_symbol);
	}

	if (!res.new_trades.trades.empty()) {
		res.currencyBalance = stock.getBalance(minfo.currency_symbol);
	} else {
		res.currencyBalance = *currency_balance_cache;
	}



	auto step = cfg.force_spread>0?cfg.force_spread:calcSpread();
	res.curStep = step;


	res.new_fees = stock.getFees(cfg.pairsymb);

	auto ticker = stock.getTicker(cfg.pairsymb);
	res.ticker = ticker;
	res.curPrice = std::sqrt(ticker.ask*ticker.bid);

	res.chartItem.time = ticker.time;
	res.chartItem.bid = ticker.bid;
	res.chartItem.ask = ticker.ask;
	res.chartItem.last = ticker.last;

	return res;
}


MTrader::Order MTrader::calculateOrderFeeLess(
		double prevPrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
		double mult) const {
	Order order;


	double newPrice = prevPrice * exp(step*dynmult);
	double newPriceNoScale= prevPrice * exp(step);

	if (step < 0) {
		//if price is lower than old, check whether current price is above
		//otherwise lower the price more
		if (newPrice > curPrice) newPrice = curPrice;
	} else {
		//if price is higher then old, check whether current price is below
		//otherwise highter the newPrice more

		if (newPrice < curPrice) newPrice = curPrice;
	}

	double size = strategy.calcOrderSize(cfg.dynmult_scale?newPrice:newPriceNoScale, balance);

	if (size * step >= 0) {
		if (cfg.dust_orders) {
			size = -sgn(step)*minfo.min_size;
		} else {
			size = 0;
		}
	}
	//fill order
	order.size = size * mult;
	order.price = newPrice;


	return order;

}

MTrader::Order MTrader::calculateOrder(
		double lastTradePrice,
		double step,
		double dynmult,
		double curPrice,
		double balance,
		double mult) const {

	Order order(calculateOrderFeeLess(lastTradePrice, step,dynmult,curPrice,balance,mult));

	if (cfg.max_size && std::fabs(order.size) > cfg.max_size) {
		order.size = cfg.max_size*sgn(order.size);
	}
	if (std::fabs(order.size) < cfg.min_size) {
		order.size = cfg.min_size*sgn(order.size);
	}
	if (std::fabs(order.size) < minfo.min_size) {
		order.size = minfo.min_size*sgn(order.size);
	}
	if (minfo.min_volume) {
		double vol = std::fabs(order.size * order.price);
		if (vol < minfo.min_volume) {
			order.size = minfo.min_volume/order.price*sgn(order.size);
		}
	}
	//apply fees
	minfo.addFees(order.size, order.price);

	//order here
	return order;

}




void MTrader::loadState() {
	minfo = stock.getMarketInfo(cfg.pairsymb);
	std::string brokerImg;
	const IBrokerIcon *bicon = dynamic_cast<const IBrokerIcon *>(&stock);
	if (bicon) brokerImg = bicon->getIconName();
	this->statsvc->setInfo(
			IStatSvc::Info {
				cfg.title,
				minfo.asset_symbol,
				minfo.currency_symbol,
				minfo.invert_price?minfo.inverted_symbol:minfo.currency_symbol,
				brokerImg,
				cfg.report_position_offset,
				minfo.invert_price,
				minfo.leverage != 0,
				minfo.simulator
			});
	currency_balance_cache = stock.getBalance(minfo.currency_symbol);
	strategy.setMarketInfo(minfo);



	if (storage == nullptr) return;
	auto st = storage->load();
	need_load = false;


	if (!cfg.dry_run) {
		json::Value t = st["test_backup"];
		if (t.defined()) {
			st = t.replace("chart",st["chart"]);
		}
	}

	if (st.defined()) {


		auto state = st["state"];
		if (state.defined()) {
			buy_dynmult = state["buy_dynmult"].getNumber();
			sell_dynmult = state["sell_dynmult"].getNumber();
			internal_balance = state["internal_balance"].getNumber();
			recalc = state["recalc"].getBool();
			std::size_t nuid = state["uid"].getUInt();
			if (nuid) uid = nuid;
			lastTradeId = state["lastTradeId"];
		}
		auto chartSect = st["chart"];
		if (chartSect.defined()) {
			chart.clear();
			for (json::Value v: chartSect) {
				double ask = v["ask"].getNumber();
				double bid = v["bid"].getNumber();
				double last = v["last"].getNumber();
				std::uint64_t tm = v["time"].getUIntLong();

				chart.push_back({tm,ask,bid,last});
			}
		}
		{
			auto trSect = st["trades"];
			if (trSect.defined()) {
				trades.clear();
				for (json::Value v: trSect) {
					TWBItem itm = TWBItem::fromJSON(v);
					trades.push_back(itm);
				}
			}
			mergeTrades(0);
		}
		strategy.importState(st["strategy"]);
		if (cfg.dry_run) {
			test_backup = st["test_backup"];
			if (!test_backup.hasValue()) {
				test_backup = st.replace("chart",json::Value());
			}
		}
	}
	tempPr.broker = cfg.broker;
	tempPr.magic = magic;
	tempPr.uid = uid;
	tempPr.currency = minfo.currency_symbol;

}

void MTrader::saveState() {
	if (storage == nullptr) return;
	json::Object obj;

	{
		auto st = obj.object("state");
		st.set("buy_dynmult", buy_dynmult);
		st.set("sell_dynmult", sell_dynmult);
		st.set("internal_balance", *internal_balance);
		st.set("recalc",recalc);
		st.set("uid",uid);
		st.set("lastTradeId",lastTradeId);
	}
	{
		auto ch = obj.array("chart");
		for (auto &&itm: chart) {
			ch.push_back(json::Object("time", itm.time)
				  ("ask",itm.ask)
				  ("bid",itm.bid)
				  ("last",itm.last));
		}
	}
	{
		auto tr = obj.array("trades");
		for (auto &&itm:trades) {
			tr.push_back(itm.toJSON());
		}
	}
	obj.set("strategy",strategy.exportState());
	if (test_backup.hasValue()) {
		obj.set("test_backup", test_backup);
	}
	storage->store(obj);
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
			wr->norm_accum = rd->norm_accum;
			wr->norm_profit = rd->norm_profit;
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

void MTrader::processTrades(Status &st) {

	StringView<IStockApi::Trade> new_trades(st.new_trades.trades);

	//Remove duplicate trades
	//which can happen by failed synchronization
	//while the new trade is already in current trades
	while (!new_trades.empty() && !trades.empty()
			&& std::find_if(trades.begin(), trades.end(),[&](const IStockApi::Trade &t) {
				return t.id == new_trades[0].id;
			}) != trades.end()) {
			new_trades = new_trades.substr(1);
	}

	if (new_trades.empty()) return;



	auto z = std::accumulate(new_trades.begin(), new_trades.end(),std::pair<double,double>(st.assetBalance,st.currencyBalance),
			[](const std::pair<double,double> &x, const IStockApi::Trade &y) {
		return std::pair<double,double>(x.first - y.eff_size, x.second + y.eff_size*y.eff_price);}
	);

	if (!strategy.isValid()) {
		strategy.init(new_trades[0].eff_price, z.first, z.second);
	}


	double last_np = 0;
	double last_ap = 0;
	if (!trades.empty()) {
		last_np = trades.back().norm_profit;
		last_ap = trades.back().norm_accum;

	}


	for (auto &&t : new_trades) {

		tempPr.tradeId = t.id.toString().str();
		tempPr.size = t.eff_size;
		tempPr.price = t.eff_price;
		if (!minfo.simulator) statsvc->reportPerformance(tempPr);
		z.first += t.eff_size;
		z.second -= t.eff_size * t.eff_price;
		auto norm = strategy.onTrade(t.eff_price, t.eff_size, z.first, z.second);
		trades.push_back(TWBItem(t, last_np+=norm.normProfit, last_ap+=norm.normAccum));
	}
}

void MTrader::update_dynmult(bool buy_trade,bool sell_trade) {

	switch (cfg.dynmult_mode) {
	case Dynmult_mode::independent:
		break;
	case Dynmult_mode::together:
		buy_trade = buy_trade || sell_trade;
		sell_trade = buy_trade;
		break;
	case Dynmult_mode::alternate:
		if (buy_trade) this->sell_dynmult = 0;
		else if (sell_trade) this->buy_dynmult = 0;
		break;
	case Dynmult_mode::half_alternate:
		if (buy_trade) this->sell_dynmult = ((this->sell_dynmult-1) * 0.5) + 1;
		else if (sell_trade) this->buy_dynmult = ((this->buy_dynmult-1) * 0.5) + 1;
		break;
	}
	this->buy_dynmult= raise_fall(this->buy_dynmult, buy_trade);
	this->sell_dynmult= raise_fall(this->sell_dynmult, sell_trade);
}

void MTrader::reset() {
	if (need_load) loadState();
	if (trades.size() > 1) {
		trades.erase(trades.begin(), trades.end()-1);
	}
	if (!trades.empty()) {
		trades.back().norm_accum = 0;
		trades.back().norm_profit = 0;
	}
	saveState();
}

void MTrader::stop() {
	cfg.enabled = false;
}


void MTrader::repair() {
	if (need_load) loadState();
	buy_dynmult = 1;
	sell_dynmult = 1;
	if (cfg.internal_balance) {
		if (!trades.empty())
			internal_balance = std::accumulate(trades.begin(), trades.end(), 0.0, [](double v, const TWBItem &itm) {return v+itm.eff_size;});
	} else {
		internal_balance.reset();
	}
	currency_balance_cache.reset();
	strategy.reset();

	if (!trades.empty()) {
		stock.reset();
		lastTradeId = nullptr;
	}
	double lastPrice = 0;
	for (auto &&x : trades) {
		if (!isfinite(x.norm_accum)) x.norm_accum = 0;
		if (!isfinite(x.norm_profit)) x.norm_profit = 0;
		if (x.price < 1e-8 || !isfinite(x.price)) {
			x.price = lastPrice;
		} else {
			lastPrice = x.price;
		}

	}
	saveState();
}

ondra_shared::StringView<IStatSvc::ChartItem> MTrader::getChart() const {
	return chart;
}


bool MTrader::acceptLoss(std::optional<Order> &orig, const Order &order, const Status &st) {

	if (cfg.accept_loss && cfg.enabled && !trades.empty()) {
		std::size_t ttm = trades.back().time;

		if (buy_dynmult <= 1.0 && sell_dynmult <= 1.0) {
			if (cfg.dust_orders) {
				Order cpy (order);
				cpy.size = sgn(cpy.size)*minfo.min_size;
				try {
					setOrder(orig,cpy);
					return true;
				} catch (...) {

				}
			}
			std::size_t e = st.chartItem.time>ttm?(st.chartItem.time-ttm)/(3600000):0;
			double lastTradePrice = trades.back().eff_price;
			if (e > cfg.accept_loss) {
				auto reford = calculateOrder(lastTradePrice, 2 * st.curStep * sgn(-order.size),1,lastTradePrice, st.assetBalance, 1.0);
				double df = (st.curPrice - reford.price)* sgn(-order.size);
				if (df > 0) {
					ondra_shared::logWarning("Accept loss in effect: price=$1, balance=$2", st.curPrice, st.assetBalance);
					trades.push_back(TWBItem (
							IStockApi::Trade {
								json::Value(json::String({vtradePrefix,"loss_", std::to_string(st.chartItem.time)})),
								st.chartItem.time,
								0,
								reford.price,
								0,
								reford.price,
							}, trades.back().norm_profit, trades.back().norm_accum));
					strategy.onTrade(reford.price, 0, st.assetBalance, st.currencyBalance);
				}
			}
		}
	}
	return false;
}

class ConfigOuput {
public:


	class Mandatory:public ondra_shared::VirtualMember<ConfigOuput> {
	public:
		using ondra_shared::VirtualMember<ConfigOuput>::VirtualMember;
		auto operator[](StrViewA name) const {
			return getMaster()->getMandatory(name);
		}
	};

	Mandatory mandatory;

	class Item {
	public:

		Item(StrViewA name, const ondra_shared::IniConfig::Value &value, std::ostream &out, bool mandatory):
			name(name), value(value), out(out), mandatory(mandatory) {}

		template<typename ... Args>
		auto getString(Args && ... args) const {
			auto res = value.getString(std::forward<Args>(args)...);
			out << name << "=" << res ;trailer();
			return res;
		}

		template<typename ... Args>
		auto getUInt(Args && ... args) const {
			auto res = value.getUInt(std::forward<Args>(args)...);
			out << name << "=" << res;trailer();
			return res;
		}
		template<typename ... Args>
		auto getNumber(Args && ... args) const {
			auto res = value.getNumber(std::forward<Args>(args)...);
			out << name << "=" << res;trailer();
			return res;
		}
		template<typename ... Args>
		auto getBool(Args && ... args) const {
			auto res = value.getBool(std::forward<Args>(args)...);
			out << name << "=" << (res?"on":"off");trailer();
			return res;
		}
		bool defined() const {
			return value.defined();
		}

		void trailer() const {
			if (mandatory) out << " (mandatory)";
			out << std::endl;
		}

	protected:
		StrViewA name;
		const ondra_shared::IniConfig::Value &value;
		std::ostream &out;
		bool mandatory;
	};

	Item operator[](ondra_shared::StrViewA name) const {
		return Item(name, ini[name], out, false);
	}
	Item getMandatory(ondra_shared::StrViewA name) const {
		return Item(name, ini[name], out, true);
	}

	ConfigOuput(const ondra_shared::IniConfig::Section &ini, std::ostream &out)
	:mandatory(this),ini(ini),out(out) {}

protected:
	const ondra_shared::IniConfig::Section &ini;
	std::ostream &out;
};

void MTrader::dropState() {
	storage->erase();
	statsvc->clear();
}

class ConfigFromJSON {
public:

	class Mandatory:public ondra_shared::VirtualMember<ConfigFromJSON> {
	public:
		using ondra_shared::VirtualMember<ConfigFromJSON>::VirtualMember;
		auto operator[](StrViewA name) const {
			return getMaster()->getMandatory(name);
		}
	};

	class Item {
	public:

		json::Value v;

		Item(json::Value v):v(v) {}

		auto getString() const {return v.getString();}
		auto getString(json::StrViewA d) const {return v.defined()?v.getString():d;}
		auto getUInt() const {return v.getUInt();}
		auto getUInt(std::size_t d) const {return v.defined()?v.getUInt():d;}
		auto getNumber() const {return v.getNumber();}
		auto getNumber(double d) const {return v.defined()?v.getNumber():d;}
		auto getBool() const {return v.getBool();}
		auto getBool(bool d) const {return v.defined()?v.getBool():d;}
		bool defined() const {return v.defined();}

	};

	Item operator[](ondra_shared::StrViewA name) const {
		return Item(config[name]);
	}
	Item getMandatory(ondra_shared::StrViewA name) const {
		json::Value v = config[name];
		if (v.defined()) return Item(v);
		else throw std::runtime_error(std::string(name).append(" is mandatory"));
	}

	Mandatory mandatory;

	ConfigFromJSON(json::Value config):mandatory(this),config(config) {}
protected:
	json::Value config;
};

double MTrader::calcSpread() const {
	if (chart.size() < 15) return 0.01;
	std::queue<double> sma;
	std::vector<double> mapped;
	std::accumulate(chart.begin(), chart.end(), 0.0, [&](auto &&a, auto &&c) {
		double h = 0.0;
		if ( sma.size() >= cfg.spread_calc_sma_hours) {
			h = sma.front();
			sma.pop();
		}
		double d = a + c.last - h;
		sma.push(c.last);
		mapped.push_back(c.last - d/sma.size());
		return d;
	});

	std::size_t i = mapped.size() >= cfg.spread_calc_stdev_hours?mapped.size()-cfg.spread_calc_stdev_hours:0;
	auto iter = mapped.begin()+i;
	auto end = mapped.end();
	auto stdev = std::sqrt(std::accumulate(iter, end, 0.0, [&](auto &&v, auto &&c) {
		return v + c*c;
	})/std::distance(iter, end));

	double lnspread = std::log((stdev+sma.back())/sma.back());
	logDebug("Spread calculated: stdev=$1, sma=$2, lnspread=$3", stdev, sma.back(), lnspread);

	return lnspread;


}
