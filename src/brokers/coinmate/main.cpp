/*
 * main.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <rpc/rpcServer.h>
#include <imtjson/operations.h>
#include "proxy.h"
#include <shared/linear_map.h>
#include <brokers/api.h>


using namespace json;

class Interface: public AbstractBrokerAPI {
public:
	mutable Proxy cm;

	Interface(const std::string &path)
		:AbstractBrokerAPI(path,{Object({
				{"name","pubKey"},
				{"label","Public key"},
				{"type", "string"}
		}),
			Object({
				{"name","privKey"},
				{"label","Private key"},
				{"type", "string"}
		}),
			Object({
				{"name","clientId"},
				{"label","Client ID"},
				{"type", "string"},
		})}) {}



	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId,
			json::Value replaceId,
			double replaceSize) override;
	virtual bool reset() override ;
	virtual MarketInfo getMarketInfo(const std::string_view & pair) override ;
	virtual double getFees(const std::string_view &pair) override ;
	virtual std::vector<std::string> getAllPairs() override ;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override {}
	virtual Interface *createSubaccount(const std::string &path) override {
		return new Interface(path);
	}
	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
	virtual AllWallets getWallet() override;
	virtual json::Value getMarkets() const override ;

	Value balanceCache;
	Value orderCache;
	Value tradeCache;
	mutable Value all_pairs;
	bool fetch_trades = true;

	struct FeeInfo {
		double fee;
		std::chrono::system_clock::time_point expiration;
	};

	
	ondra_shared::linear_map<String, FeeInfo> feeMap;

	virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
			IHistoryDataSource::HistData &data) override;
	json::Value findSymbol(const std::string_view &asset, const std::string_view &currency);
};

 double Interface::getBalance(const std::string_view &symb, const std::string_view & ) {
	 if (!balanceCache.defined()) {
		 	balanceCache = cm.request(Proxy::POST, "balances", Value());
	 }
	 return balanceCache[symb]["balance"].getNumber();
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	cm.privKey = keyData["privKey"].getString();
	cm.pubKey = keyData["pubKey"].getString();
	cm.clientid = keyData["clientId"].getString();
	balanceCache = Value();
	orderCache = Value();
	tradeCache = Value();
	all_pairs = Value();
	fetch_trades = true;

}

static bool greaterThanId(Value tx, Value id) {
	return Value::compare(id, tx["transactionId"]) < 0;
}
static int sortById(Value tx1, Value tx2) {
	return Value::compare(tx1["transactionId"],tx2["transactionId"]);
}

Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view & pair) {

		if (!tradeCache.hasValue()) {
			json::Value args (json::object, {
					json::Value("limit",10)
			});
			Value c =  cm.request(Proxy::POST, "tradeHistory", args).sort(sortById);

			if (c.empty()) return TradesSync{ {}, nullptr };

			tradeCache = c;
			fetch_trades = false;
		}

		if (lastId.hasValue() && greaterThanId(tradeCache[0], lastId)) {

			json::Value args (json::object, {
					json::Value("sort", "ASC"),
					json::Value("lastId",lastId),
					json::Value("limit",1000)
			});
			tradeCache = cm.request(Proxy::POST, "tradeHistory", args);
			fetch_trades = false;
		}

		Value l = tradeCache[tradeCache.size()-1]["transactionId"];


		if (fetch_trades) {

			json::Value args (json::object, {
					json::Value("sort", "ASC"),
					json::Value("lastId",l),
					json::Value("limit",1000)
			});
			tradeCache = tradeCache.merge(cm.request(Proxy::POST, "tradeHistory", args));
			fetch_trades = false;
			l = tradeCache[tradeCache.size()-1]["transactionId"];
		}


		if (lastId.hasValue()) {

			Value trades = tradeCache.filter([&](Value v) {
				return v["currencyPair"] == pair && greaterThanId(v, lastId);
			});

			return TradesSync{
				mapJSON<TradeHistory> (trades, [&](Value x){
					double mlt = x["type"].getString() == "SELL"?-1:1;
					double price = x["price"].getNumber();
					double fee = x["fee"].getNumber();
					double size = x["amount"].getNumber()*mlt;
					double eff_price = price + fee/size;
					return Trade {
						x["transactionId"],
						x["createdTimestamp"].getUIntLong(),
						size,
						price,
						size,
						eff_price
					};
				}), l};

		} else {
			return TradesSync{ {}, l};
		}

}


 Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {


	 	 if (!orderCache.defined()) {
	 		 orderCache = cm.request(Proxy::POST, "openOrders", Value());
	 	 }

	 	 return  mapJSON<Orders>(orderCache.filter([&](Value order) {
	 		 return order["currencyPair"].getString() == pair;
	 	 }),[](Value order) {
			double mlt = order["type"].getString() == "SELL"?-1:1;
			return Order {order["id"],
						  order["clientOrderId"],
						  order["amount"].getNumber()*mlt,
						  order["price"].getNumber()
			};
		});
}

 Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	 json::Value args (json::object, {json::Value("currencyPair",pair)});
	 Value r = cm.request(Proxy::GET, "ticker", args);
	 return Ticker{
		 r["bid"].getNumber(),
		 r["ask"].getNumber(),
		 r["last"].getNumber(),
		 r["timestamp"].getUIntLong()*1000 //HACK - time is not in milliseconds
	 };
}

static const char *place[] = {"sellLimit","buyLimit"};
//static const char *replace[] = {"replaceBySellLimit","replaceByBuyLimit"};

static Value number_to_decimal(double v, unsigned int precision) {
    std::ostringstream buff;
    buff.precision(precision);
    buff << std::fixed << v;
    return buff.str();
}


json::Value Interface::placeOrder(const std::string_view & pair,
		double size,
		double price,
		json::Value clientId,
		json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {
		Value res = cm.request(Proxy::POST, "cancelOrderWithInfo",Object({{"orderId", replaceId}}));
		if (replaceSize && (
				res["success"].getBool() != true
				|| res["remainingAmount"].getNumber()<std::fabs(replaceSize)*0.999999)) {
				return null;
		}
	}

	const char *cmd = nullptr;

	double amount;
	const char **cmdset = place;
	if (size<0) {
		cmd = cmdset[0];
		amount = -size;
	} else if (size > 0){
		cmd = cmdset[1];
		amount = +size;
	} else {
		return null;
	}

	if (!all_pairs.defined()) {
	    getAllPairs();
	}
	
    auto iter = std::find_if(all_pairs.begin(), all_pairs.end(), [&](Value v) {
        return v["name"].getString() == pair;
    });
    if (iter == all_pairs.end()) throw std::runtime_error("Pair not found");
    Value pinfo(*iter);
    int price_precs = pinfo["priceDecimals"].getInt();
    int amount_precs = pinfo["lotDecimals"].getInt();
	
	
	json::Value args(json::object,{
		json::Value("amount", number_to_decimal(amount, amount_precs)),
		json::Value("price", number_to_decimal(price, price_precs)),
		json::Value("currencyPair", pair),
		json::Value("clientOrderId",clientId)
	});

	auto resp = cm.request(Proxy::POST, cmd, args);
	return resp;
}

bool Interface::reset() {
	balanceCache = Value();
	orderCache = Value();
	fetch_trades = true;
	return true;
}

std::vector<std::string> Interface::getAllPairs() {
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	return mapJSON< std::vector<std::string> >(all_pairs, [&](Value z){
		return z["name"].toString().str();
	});
}

double Interface::getFees(const std::string_view &pair) {
	if (cm.hasKey()) {
		auto now = std::chrono::system_clock::now();
		auto iter = feeMap.find(pair);
		if (iter == feeMap.end() || iter->second.expiration < now) {
			Value fresp = cm.request(Proxy::POST, "traderFees", Object({{"currencyPair", pair}}));
			FeeInfo &fi = feeMap[pair];
			fi.fee = fresp["maker"].getNumber()*0.01;
			fi.expiration = now + std::chrono::hours(1);
			return fi.fee;
		} else {
			return iter->second.fee;
		}
	} else {
		return 0.0012;
	}
}

Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	auto iter = std::find_if(all_pairs.begin(), all_pairs.end(), [&](Value v) {
		return v["name"].getString() == pair;
	});
	if (iter == all_pairs.end()) throw std::runtime_error("No such pair. Use 'getAllPairs' to enlist all available pairs");
	Value res(*iter);
	return MarketInfo {
			res["firstCurrency"].getString(),
			res["secondCurrency"].getString(),
			pow(10,-res["lotDecimals"].getNumber()),
			pow(10,-res["priceDecimals"].getNumber()),
			res["minAmount"].getNumber(),
			0,
			getFees(pair)
	};
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		cm.hasKey(),
		"coinmate",
		"Coinmate",
		"https://coinmate.io?referral=U1RjMGVTMUljV2h3VmxkWk0xbHViWFF6WHkxcFFRPT0",
		"1.0",
		R"mit(Copyright (c) 2019 Ondřej Novák

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.)mit",
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAD1BMVEUAAAAsYnlHi6FfqcNXwdo8"
"rDwaAAAAAXRSTlMAQObYZgAAAqpJREFUeAHtmle2IyEQQ5+uvP8tT+ozNQF3OYBeRP9Gt0LjOsDL"
"1gfW1tbW1hZCEm9gDNi2Lz9k2wZeiUOCw3iQAYXtr7uXbEcR1LiXcgzicqcSCBLuXbMI1+z9t0YC"
"ov42SKr0AHYwCfK/5tIdLWovJMAn7i2D0WqC0b5HYGERfO8uA07kgMa+2zG8jEB6tGKHzMtbSPQ5"
"yEv4axH0jUCQQAC6SRAN0+4Jsp8Cv9Zv/kTSbeB2ecnhFIg+PpElEL4xfohYESRs/zOhtG2AwhOy"
"jdQQWDH7HqGKQNC+ZJRPQTuh2zrdj4j4D1O5yaZA/tuLX26A3RDURLnU33CSGJNLgZq/WHE6ALBq"
"OxTt3xs+6UStakPc51I++Rh//25RAaw+RWggX9IEumMZ2VWe5d+B7llFXMVjQRPUInr+t16RAM38"
"eD4BJkrfSEwtMd+F8tRgM78VMbcCiwCsDwIQ+A79xgB6cwA2wIcECHyGvB2A8zth8jv0PMBcDQp/"
"ugmsiQJa6ZHwFsCKmdCK9mAj+empVJ4HuG+6l1aVr1mmWQdb09VrhNurGOGLTaQCFUlDUKeGylSg"
"CAphsD9khSpQNsPtqQT2cDoQSEARFMQhd+dkono3c0rWnxQGDgrx7Yv7+FHpqX97rrNO4Ltfb7jY"
"Yof17b06Dl4Y4HKv4GP3Bc3yrtg7f+Uvrbq9W9FrO93cuXmbe0OWNIBaANum958sAD29ijBzcyue"
"zZ+W+R8EM/5aMIbqTf37VVr7yQ+geRKU828I9LC9WTWId6s116po8Us6uD/6xBTaV0Iw0K6Q5OE1"
"I/oHQxrfNJrkg1KbX5L4Jbu/xA4/Ku2H4whBL6O3etZcs3FG3GRwyL4kcGNP0r0EtseuT7oPkgB8"
"qPaF15YOTVtvbW1tbW1tbX0HniAh/KR9ZWcAAAAASUVORK5CYII=",false,true
	};
}


Interface::AllWallets Interface::getWallet() {
	getBalance("","");
	Wallet w;
	for (Value x: balanceCache) {
		double n = x["balance"].getNumber();
		if (n) {
			String symb = x.getKey();
			w.wallet.push_back({
				symb, n
			});
		}
	}
	AllWallets aw;
	w.walletId ="exchange";
	aw.push_back(w);
	return aw;
}

json::Value Interface::getMarkets() const  {
	ondra_shared::linear_map<std::string_view, json::Object> smap;
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	for (json::Value row: all_pairs) {
		smap[row["firstCurrency"].getString()].set(row["secondCurrency"].getString(), row["name"]);
	}
	return json::Value(json::object, smap.begin(), smap.end(), [](const auto &x){
		return json::Value(x.first, x.second);
	});

}

int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required one argument" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();
}

json::Value Interface::findSymbol(const std::string_view &asset, const std::string_view &currency) {
	if (!all_pairs.defined()) {
		all_pairs = cm.request(Proxy::GET, "tradingPairs", Value());
	}
	json::Value f = all_pairs.find([&](json::Value row){
		auto first = row["firstCurrency"];
		auto second = row["secondCurrency"];
		return  (first.getString() == asset && second.getString() == currency) || (first.getString() == currency && second.getString() == asset);
	});
	return f;

}

inline bool Interface::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	return findSymbol(asset, currency).defined();
}

struct HistDataSet{
	int minutes;
	int interval;
	int duplcnt;

	std::uint64_t toMS() const {return static_cast<std::uint64_t>(minutes)*60000;}
};

HistDataSet histDataSets[] = {
		{150*5, 5, 1},
		{150*15, 15, 3},
		{150*30, 30, 6},
		{150*60, 60, 12},
		{150*120, 120, 24},
		{150*240, 240, 48},
		{150*360, 360, 72},
		{150*720, 720, 144},
		{150*1440, 1440, 288},
		{150*4320, 4320, 864},
		{150*10080, 10080, 2016}
};

inline uint64_t Interface::downloadMinuteData(const std::string_view &asset,
		const std::string_view &currency, const std::string_view &hint_pair, uint64_t time_from,
		uint64_t time_to, HistData &xdata) {
	auto f =findSymbol(asset, currency);
	auto name = f["name"];
	auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	auto hpt = now - time_to;
	auto st = std::find_if(std::begin(histDataSets), std::end(histDataSets),[&](const HistDataSet &x){
		return x.toMS() > hpt;
	});
	HTTPJson hapi(cm.httpc);
	hapi.setBaseUrl("https://coinmate.io/");
	if (st == std::end(histDataSets)) return 0;
	std::uint64_t start = std::max(now - st->toMS(), time_from);
	json::Value hdata = hapi.GET(std::string("guirest/rateGraph?currencyPairName=").append(name.getString()).append("&interval=").append(std::to_string(st->interval)));

	MinuteData data;
	
	auto insert_val = [&,inv=f["firstCurrency"].getString() == currency](double n){
			if (inv) n=1/n;
			for (int i = 0; i < st->duplcnt; i++) data.push_back(n);
		};

	for (Value row: hdata) {
		auto date = row["date"].getUIntLong();
		if (date >= start && date < time_to) {
			double o = row["open"].getNumber();
			double h = row["high"].getNumber();
			double l = row["low"].getNumber();
			double c = row["close"].getNumber();
			double m = std::sqrt(h*l);
			insert_val(o);
			insert_val(h);
			insert_val(m);
			insert_val(l);
			insert_val(c);
		}
	}
	xdata = std::move(data);
	return start;

}
