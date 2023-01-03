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
#include "shared/toString.h"
#include "proxy.h"
#include <cmath>
#include <ctime>
#include <deque>

#include "../api.h"
#include <imtjson/stringValue.h>
#include <shared/linear_map.h>
#include <shared/iterator_stream.h>
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>
#include "../../shared/logOutput.h"
#include "names.h"


using namespace json;

static Value keyFormat = {Object({
							{"name","pubKey"},
							{"type","string"},
							{"label","Public key"}}),
						 Object({
							{"name","privKey"},
							{"type","string"},
							{"label","Private key"}})};

static std::string_view COIN_M_PREFIX = "COIN-M:";
static std::string_view USDT_M_PREFIX = "USDT-M:";

static std::string_view remove_prefix(const std::string_view &pair) {
	auto p =  pair.find(':');
	if (p == pair.npos) return pair;
	else return pair.substr(p+1);
}


class Interface: public AbstractBrokerAPI {
public:
	Proxy px;
	Proxy dapi;
	Proxy fapi;


	Interface(const std::string &path):AbstractBrokerAPI(path, keyFormat)
		,px("https://api.binance.com", "/api/v3/time")
		,dapi("https://dapi.binance.com", "/dapi/v1/time")
		,fapi("https://fapi.binance.com", "/fapi/v1/time")
	{}


	virtual double getBalance(const std::string_view & symb, const std::string_view & pair) override;
	virtual TradesSync syncTrades(json::Value lastId, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par)override;
	virtual Ticker getTicker(const std::string_view & piar)override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size,
			double price,
			json::Value clientId,
			json::Value replaceId,
			double replaceSize)override;
	virtual bool reset()override;
	virtual MarketInfo getMarketInfo(const std::string_view & pair)override;
	virtual double getFees(const std::string_view &pair)override;
	virtual std::vector<std::string> getAllPairs()override;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;
	virtual Interface *createSubaccount(const std::string &path) override {
		return new Interface(path);
	}
	virtual json::Value getMarkets() const override;
	virtual AllWallets getWallet() override {return {};};
	virtual json::Value getWallet_direct()  override;
	virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) override;
	virtual std::uint64_t downloadMinuteData(const std::string_view &asset,
					  const std::string_view &currency,
					  const std::string_view &hint_pair,
					  std::uint64_t time_from,
					  std::uint64_t time_to,
					  HistData &data
				) override;


	enum class Category {
		spot, coin_m, usdt_m
	};

	struct MarketInfoEx: MarketInfo {
		unsigned int size_precision;
		unsigned int quote_precision;
		Category cat;
		std::string label; //for futures
		std::string type; //for futures
	};

	using Symbols = ondra_shared::linear_map<std::string, MarketInfoEx, std::less<std::string_view> > ;
	using Tickers = ondra_shared::linear_map<std::string, Ticker,  std::less<std::string_view> >;

	Value balanceCache;
	Tickers tickerCache;
	Value orderCache;
	Value feeInfo;
	std::chrono::system_clock::time_point feeInfoExpiration;
	Symbols symbols;


	void updateBalCache();
	Value generateOrderId(Value clientId);

	std::uintptr_t idsrc;

	void initSymbols();

	Value dapi_readAccount();
	Value fapi_readAccount();
	std::chrono::steady_clock::time_point symbolsExpire;

	virtual void restoreSettings(json::Value v) override;
	virtual json::Value setSettings(json::Value v) override;
	virtual json::Value getSettings(const std::string_view &pairHint) const override;

	bool feesInBnb = false;

	static bool isMatchedPair(const MarketInfo &minfo, const std::string_view &asset, const std::string_view &currency);

protected:
	bool dapi_isSymbol(const std::string_view &pair);
	double dapi_getFees();
	json::Value dapi_getLeverage(const std::string_view &pair);
	double dapi_getPosition(const std::string_view &pair);
	double dapi_getCollateral(const std::string_view &pair);

	bool fapi_isSymbol(const std::string_view &pair);
	double fapi_getPosition(const std::string_view &pair);
	double fapi_getFees();
	double fapi_getCollateral(const std::string_view &currency);
	json::Value fapi_getLeverage(const std::string_view &pair);


private:

	Value dapi_account;
	Value dapi_positions;
	Tickers dapi_tickers;

	Value fapi_account;
	Value fapi_positions;
	Tickers fapi_tickers;
	
	using OrderMap=std::map<std::string, std::vector<Order> >;
	OrderMap orderMap;


};


void Interface::updateBalCache() {
	 if (!balanceCache.defined()) {
		 balanceCache = px.private_request(Proxy::GET,"/api/v3/account",json::object);
		 feeInfo = balanceCache["makerCommission"].getNumber()/10000.0;
		 Object r;
		 for (Value x : balanceCache["balances"]) {
			 r.set(x["asset"].getString(), x);
		 }
		 balanceCache = balanceCache.replace("balances", r);
	 }
}


 double Interface::getBalance(const std::string_view & symb, const std::string_view & pair) {
	 if (dapi_isSymbol(pair)) {
		initSymbols();
		auto iter = symbols.find(pair);
		if (iter == symbols.end()) throw std::runtime_error("No such symbol");
		const MarketInfo &minfo = iter->second;
		if (minfo.asset_symbol == symb) return dapi_getPosition(remove_prefix(pair))*minfo.asset_step;
		else return dapi_getCollateral(symb);
	 } else  if (fapi_isSymbol(pair)) {
		initSymbols();
		auto iter = symbols.find(pair);
		if (iter == symbols.end()) throw std::runtime_error("No such symbol");
		const MarketInfo &minfo = iter->second;
		if (minfo.asset_symbol == symb) return fapi_getPosition(remove_prefix(pair));
		else return fapi_getCollateral(symb);
	 } else {
		 updateBalCache();
		 Value v =balanceCache["balances"][symb];
		 if (v.defined()) return v["free"].getNumber()+v["locked"].getNumber();
		 else throw std::runtime_error("No such symbol");
	 }
}


static json::Value readTrades(Proxy &proxy, const std::string &command, std::string_view pair, Value &lastId) {
	std::uint64_t maxID = lastId.getUIntLong();
	if (maxID) {
		Value r = proxy.private_request(Proxy::GET, command, Object({
				{"symbol",pair},
				{"fromId",lastId},
				{"limit",10}})
				);
		for (Value x: r) {
			Value id = x["id"];
			std::uint64_t mid = id.getUIntLong();
			if (mid>=maxID) maxID = mid+1;
		}
		lastId = maxID;
		return r;
	} else {
		Value r = proxy.private_request(Proxy::GET, command, Object({
				{"symbol",pair},
				{"limit",1}})
				);
		if (r.empty()) {
			lastId = 1;
		} else {
			lastId = r[0]["id"].getUIntLong()+1;
		}
		return json::array;
	}
}


 Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view & pair) {
	 initSymbols();
	 auto iter = symbols.find(pair);
	 if (iter == symbols.end())
		 throw std::runtime_error("No such symbol");

	 const MarketInfo &minfo = iter->second;

	 if (dapi_isSymbol(pair)) {
		 auto cpair = remove_prefix(pair);
		 Value r = readTrades(dapi, "/dapi/v1/userTrades", cpair, lastId);
		 TradeHistory h(mapJSON(r,[&](Value x){
			 double size = -x["qty"].getNumber()*minfo.asset_step;
			 double price = 1.0/x["price"].getNumber();
			 if (!x["buyer"].getBool()) size = -size;
			 double comms = x["commission"].getNumber();
			 double eff_size = size;
			 double eff_price = price;
			 eff_price += comms/size;

			 return Trade {
				 x["id"],
				 x["time"].getUIntLong(),
				 size,
				 price,
				 eff_size,
				 eff_price
			 };
		 }, TradeHistory()));

		 std::sort(h.begin(), h.end(),[&](const Trade &a, const Trade &b) {
			 return Value::compare(a.id,b.id) < 0;
		 });

		 return TradesSync{
			 h,
			 lastId
		 };

	 } else if (fapi_isSymbol(pair)) {
		 auto cpair = remove_prefix(pair);
		 Value r = readTrades(fapi, "/fapi/v1/userTrades", cpair, lastId);
		 TradeHistory h(mapJSON(r,[&](Value x){
			 double size = x["qty"].getNumber();
			 double price = x["price"].getNumber();
			 if (!x["buyer"].getBool()) size = -size;
			 double comms = x["commission"].getNumber();
			 double eff_size = size;
			 double eff_price = price;
			 if (x["commissionAsset"].getString() == minfo.currency_symbol) {
				 eff_price += comms/size;
			 }

			 return Trade {
				 x["id"],
				 x["time"].getUIntLong(),
				 size,
				 price,
				 eff_size,
				 eff_price
			 };
		 }, TradeHistory()));

		 std::sort(h.begin(), h.end(),[&](const Trade &a, const Trade &b) {
			 return Value::compare(a.id,b.id) < 0;
		 });

		 return TradesSync{
			 h,
			 lastId
		 };
	 } else {
		 Value r = readTrades(px, "/api/v3/myTrades", pair, lastId);

		 TradeHistory h(mapJSON(r,[&](Value x){
			 double size = x["qty"].getNumber();
			 double price = x["price"].getNumber();
			 StrViewA comass = x["commissionAsset"].getString();
			 if (!x["isBuyer"].getBool()) size = -size;
			 double comms = x["commission"].getNumber();
			 double eff_size = size;
			 double eff_price = price;
			 if (comass == StrViewA(minfo.asset_symbol)) {
				 eff_size -= comms;
				 eff_price =  std::abs(size * price / eff_size);
			 } else if (comass == StrViewA(minfo.currency_symbol)) {
				 eff_price += comms/size;
			 }

			 return Trade {
				 x["id"],
				 x["time"].getUIntLong(),
				 size,
				 price,
				 eff_size,
				 eff_price
			 };
		 }, TradeHistory()));

		 std::sort(h.begin(), h.end(),[&](const Trade &a, const Trade &b) {
			 return Value::compare(a.id,b.id) < 0;
		 });

		 return TradesSync{
			 h,
			 lastId
		 };
	 }
}


static Value extractOrderID(StrViewA id) {
	if (id.begins("mmbot")) {
		Value bin = base64url->decodeBinaryValue(id.substr(5));
		try {
			auto stream = json::fromBinary(bin.getBinary(base64url));
			return Value::parseBinary([&]{
					int c = stream();
					if (c == -1) throw 0;
					return c;
			},json::base64url)[1];
		} catch (...) {
			return Value();
		}
	} else {
		return Value();
	}

}

/*static void print_order_table(std::string_view pair, const Interface::Orders &orders) {
	for(const auto &c: orders) {
		std::cerr << "OrderTable - Pair: " << pair << ", Id: "<< c.id.toString() << " size=" << c.size << " price=" << c.price << std::endl;
	}
}*/

Interface::Orders Interface::getOpenOrders(const std::string_view & pair) {
	if (dapi_isSymbol(pair)) {
		initSymbols();
		auto cpair = remove_prefix(pair);
		auto iter = symbols.find(pair);
		if (iter == symbols.end()) throw std::runtime_error("Unknown symbol");
		const MarketInfo &minfo = iter->second;
		Value resp = dapi.private_request(Proxy::GET,"/dapi/v1/openOrders", Object({{"symbol",cpair}}));
		return mapJSON(resp, [&](Value x) {
			Value id = x["clientOrderId"];
			Value eoid = extractOrderID(id.getString());
			return Order {
				x["orderId"],
				eoid,
				(x["side"].getString() == "SELL"?1:-1)*(x["origQty"].getNumber() - x["executedQty"].getNumber())*minfo.asset_step,
				1.0/x["price"].getNumber()
			};
		}, Orders());
	} else if (fapi_isSymbol(pair)) {
		auto cpair = remove_prefix(pair);
		Value resp = fapi.private_request(Proxy::GET,"/fapi/v1/openOrders", Object({{"symbol",cpair}}));
		return mapJSON(resp, [&](Value x) {
			Value id = x["clientOrderId"];
			Value eoid = extractOrderID(id.getString());
			return Order {
				x["orderId"],
				eoid,
				(x["side"].getString() == "SELL"?-1:1)*(x["origQty"].getNumber() - x["executedQty"].getNumber()),
				x["price"].getNumber()
			};
		}, Orders());

	} else {
		json::String spair(pair);
		Orders orders;

		json::Value resp = px.private_request(Proxy::GET,"/api/v3/openOrders", Object({{"symbol",spair}}));
		orders =  mapJSON(resp, [&](Value x) {
			Value id = x["clientOrderId"];
			Value eoid = extractOrderID(id.getString());
			return Order {
				x["orderId"],
				eoid,
				(x["side"].getString() == "SELL"?-1:1)*(x["origQty"].getNumber() - x["executedQty"].getNumber()),
				x["price"].getNumber()
			};
		}, Orders());


		auto ooiter = orderMap.find(std::string(pair));
		if (ooiter != orderMap.end()) {
			std::vector<Order> new_oo;
			for (auto &c: ooiter->second) {
				if (std::find_if(orders.begin(), orders.end(),[&](const Order &d){
					return d.id == c.id;
				}) != orders.end()) {
					new_oo.push_back(c);
				} else {
					try {
						Value resp = px.private_request(Proxy::GET,"/api/v3/order", Object({{"symbol",spair},{"orderId", c.id}}));
						auto status = resp["status"];
						if (status == "NEW" || status == "PARTIALLY_FILLED") {
							c.size = (resp["side"].getString() == "SELL"?-1:1)*(resp["origQty"].getNumber() - resp["executedQty"].getNumber());
							new_oo.push_back(c);
							orders.insert(orders.begin(),c);
						}
					} catch (std::exception &e) {
						if (strstr(e.what(),"-2013") != 0) {
							throw std::runtime_error("Market is overloaded!");
						}
					}
				}
			}
			std::swap(ooiter->second, new_oo);
		}

		//print_order_table(pair, orders);
		return orders;
	}
}

static std::uint64_t now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch()
						 ).count();
}

static Value indexBySymbol(Value data) {
	Object bld;
	for (Value d:data) bld.set(d["symbol"].getString(), d);
	return bld;
}

Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	if (dapi_isSymbol(pair)) {
		auto cpair = remove_prefix(pair);
		if (dapi_tickers.empty()) {
			std::vector<Tickers::value_type> tk;
			Value book = dapi.public_request("/dapi/v1/ticker/bookTicker",Value());
			for (Value v: book) {
				double bid = 1.0/v["askPrice"].getNumber();
				double ask = 1.0/v["bidPrice"].getNumber();
				double midl = std::sqrt(bid * ask);
				tk.emplace_back(
					v["symbol"].getString(),
					Ticker{bid,ask,midl,v["time"].getUIntLong()}
				);
			}
			dapi_tickers = Tickers(std::move(tk));
		}

		 auto iter=dapi_tickers.find(cpair);
		 if (iter != dapi_tickers.end()) return iter->second;
		 else throw std::runtime_error("No such symbol");
	} else if (fapi_isSymbol(pair)) {
		auto cpair = remove_prefix(pair);
		if (fapi_tickers.empty()) {
			std::vector<Tickers::value_type> tk;
			Value book = fapi.public_request("/fapi/v1/ticker/bookTicker",Value());
			for (Value v: book) {
				double bid = v["bidPrice"].getNumber();
				double ask = v["askPrice"].getNumber();
				double midl = std::sqrt(bid * ask);
				tk.emplace_back(
					v["symbol"].getString(),
					Ticker{bid,ask,midl,v["time"].getUIntLong()}
				);
			}
			fapi_tickers = Tickers(std::move(tk));
		}

		 auto iter=fapi_tickers.find(cpair);
		 if (iter != fapi_tickers.end()) return iter->second;
		 else throw std::runtime_error("No such symbol");

	} else {
		if (tickerCache.empty()) {

			 Value book = indexBySymbol(px.public_request("/api/v3/ticker/bookTicker", Value()));
			 Value price = indexBySymbol(px.public_request("/api/v3/ticker/price", Value()));
			 auto bs = ondra_shared::iterator_stream(book);
			 auto ps = ondra_shared::iterator_stream(price);
			 std::vector<Tickers::value_type> tk;
			 while (!!bs && !!ps) {
				 Value b = *bs;
				 Value p = *ps;
				 if (b.getKey() < p.getKey()) bs();
				 else if (b.getKey() > p.getKey()) ps();
				 else {
					 tk.push_back(Tickers::value_type(
							 p.getKey(),
							 {
									 b["bidPrice"].getNumber(),
									 b["askPrice"].getNumber(),
									 p["price"].getNumber(),
									 now(),
							 }));
					 bs();
					 ps();
				 }
			 }
			 tickerCache = Tickers(std::move(tk));
		 }

		 auto iter=tickerCache.find(pair);
		 if (iter != tickerCache.end()) return iter->second;
		 else throw std::runtime_error("No such pair");
	}
}

std::vector<std::string> Interface::getAllPairs() {
	initSymbols();
 	 std::vector<std::string> res;
	 for (auto &&v: symbols) res.push_back(v.first);
	 return res;
 }

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

	initSymbols();
	auto iter = symbols.find(pair);
	if (iter == symbols.end()) throw std::runtime_error("Unknown symbol");
	if (dapi_isSymbol(pair)) {
		auto cpair = remove_prefix(pair);
		size = -size/iter->second.asset_step;
		replaceSize = replaceSize/iter->second.asset_step;
		price = std::round((1.0/price)/iter->second.currency_step)*iter->second.currency_step;

		if (replaceId.defined()) {
			Value r = dapi.private_request(Proxy::DELETE,"/dapi/v1/order",Object({
					{"symbol", cpair},
					{"orderId", replaceId}}));
			double remain = r["origQty"].getNumber() - r["executedQty"].getNumber();
			if (r["status"].getString() != "CANCELED"
					|| remain < std::fabs(replaceSize)*0.9999) return nullptr;
		}

		if (size == 0) return nullptr;

		Value orderId = generateOrderId(clientId);
		dapi.private_request(Proxy::POST,"/dapi/v1/order",Object({
				{"symbol", cpair},
				{"side", size<0?"SELL":"BUY"},
				{"type","LIMIT"},
				{"newClientOrderId",orderId},
				{"quantity", number_to_decimal(std::fabs(size),iter->second.size_precision)},
				{"price", number_to_decimal(std::fabs(price), iter->second.quote_precision)},
				{"timeInForce","GTX"},
				{"positionSide","BOTH"}})
				);

		return orderId;

	} else 	if (fapi_isSymbol(pair)) {
		auto cpair = remove_prefix(pair);
		if (replaceId.defined()) {
			Value r = fapi.private_request(Proxy::DELETE,"/fapi/v1/order",Object({
					{"symbol", cpair},
					{"orderId", replaceId}}));
			double remain = r["origQty"].getNumber() - r["executedQty"].getNumber();
			if (r["status"].getString() != "CANCELED"
					|| remain < std::fabs(replaceSize)*0.9999) return nullptr;
		}
		if (size == 0) return nullptr;
		Value orderId = generateOrderId(clientId);
		fapi.private_request(Proxy::POST,"/fapi/v1/order",Object({
				{"symbol", cpair},
				{"side", size<0?"SELL":"BUY"},
				{"type","LIMIT"},
				{"newClientOrderId",orderId},
				{"quantity", number_to_decimal(std::fabs(size),iter->second.size_precision)},
				{"price", number_to_decimal(std::fabs(price), iter->second.quote_precision)},
				{"timeInForce","GTX"},
				{"positionSide","BOTH"}})
				);

		return orderId;

	} else {

		if (replaceId.defined()) {
			Value r = px.private_request(Proxy::DELETE,"/api/v3/order",Object({
					{"symbol", pair},
					{"orderId", replaceId}}));
			double remain = r["origQty"].getNumber() - r["executedQty"].getNumber();
			if (r["status"].getString() != "CANCELED") return nullptr;
			auto &om = orderMap[std::string(pair)];
			auto itr = std::remove_if(om.begin(), om.end(), [&](const Order &c){
				return c.id == replaceId;
			});
			om.erase(itr, om.end());
			if(remain < std::fabs(replaceSize)*0.9999) return nullptr;
		}

		if (size == 0) return nullptr;

		Value clientOrderId = generateOrderId(clientId);
		json::Value resp = px.private_request(Proxy::POST,"/api/v3/order",Object({
				{"symbol", pair},
				{"side", size<0?"SELL":"BUY"},
				{"type","LIMIT_MAKER"},
				{"newClientOrderId",clientOrderId},
				{"quantity", number_to_decimal(std::fabs(size),iter->second.size_precision)},
				{"price", number_to_decimal(std::fabs(price), iter->second.quote_precision)},
				{"newOrderRespType","ACK"}}));

		Value orderId = resp["orderId"];
		orderMap[std::string(pair)].push_back(Order{
			orderId, clientId, size, price,
		});
		return orderId;
	}
}

bool Interface::reset() {
	balanceCache = Value();
	tickerCache.clear();
	orderCache = Value();
	dapi_account = Value();
	dapi_positions = Value();
	dapi_tickers.clear();
	fapi_account=Value();
	fapi_positions=Value();
	fapi_tickers.clear();
	return true;
}

void Interface::initSymbols() {
	auto now = std::chrono::steady_clock::now();
	if (symbols.empty() || symbolsExpire < now) {
			Value res = px.public_request("/api/v1/exchangeInfo",Value());

			using VT = Symbols::value_type;
			std::vector<VT> bld;
			for (Value smb: res["symbols"]) {
				std::string symbol = smb["symbol"].getString();
				MarketInfoEx nfo;
				nfo.asset_symbol = smb["baseAsset"].getString();
				nfo.currency_symbol = smb["quoteAsset"].getString();
				nfo.currency_step = std::pow(10,-smb["quotePrecision"].getNumber());
				nfo.asset_step = std::pow(10,-smb["baseAssetPrecision"].getNumber());
				nfo.feeScheme = income;
				nfo.min_size = 0;
				nfo.min_volume = 0;
				nfo.fees = getFees(symbol);
				nfo.size_precision = smb["baseAssetPrecision"].getUInt();
				nfo.quote_precision = smb["quotePrecision"].getUInt();
				for (Value f: smb["filters"]) {
					auto ft = f["filterType"].getString();
					if (ft == "LOT_SIZE") {
						nfo.min_size = f["minQty"].getNumber();
						nfo.asset_step = f["stepSize"].getNumber();
					} else if (ft == "PRICE_FILTER") {
						nfo.currency_step = f["tickSize"].getNumber();
					} else if (ft == "MIN_NOTIONAL") {
						nfo.min_volume = f["minNotional"].getNumber();
					}
				}
				nfo.cat = Category::spot;
				nfo.wallet_id="spot";

				if (feesInBnb) {
					if (nfo.asset_symbol == "BNB") nfo.feeScheme = assets;
					else nfo.feeScheme = currency;
				}

				bld.push_back(VT(symbol, nfo));
			}
			need_more_time();
			res = dapi.public_request("/dapi/v1/exchangeInfo",Value());
			std::string symbol(COIN_M_PREFIX);
			for (Value smb: res["symbols"]) {
				symbol.resize(COIN_M_PREFIX.length());
				symbol.append(smb["symbol"].getString());
				MarketInfoEx nfo;
				nfo.asset_symbol = smb["quoteAsset"].getString();
				nfo.currency_symbol = smb["marginAsset"].getString();
				nfo.currency_step = std::pow(10,-smb["pricePrecision"].getNumber());
				nfo.asset_step = smb["contractSize"].getNumber();
				nfo.feeScheme = currency;
				nfo.fees = getFees(symbol);
				nfo.min_size = nfo.asset_step;
				nfo.min_volume = 0;
				nfo.size_precision = 0;
				nfo.quote_precision = smb["quotePrecision"].getUInt();
				for (Value f: smb["filters"]) {
					auto ft = f["filterType"].getString();
					if (ft == "LOT_SIZE") {
						nfo.min_size = f["minQty"].getNumber()*nfo.asset_step;
					} else if (ft == "PRICE_FILTER") {
						nfo.currency_step = f["tickSize"].getNumber();
					} else if (ft == "MIN_NOTIONAL") {
						nfo.min_volume = f["minNotional"].getNumber();
					}
				}
				nfo.leverage = 20;
				nfo.invert_price = true;
				nfo.inverted_symbol = smb["quoteAsset"].getString();
				nfo.cat = Category::coin_m;
				nfo.label = nfo.currency_symbol+"/"+nfo.asset_symbol;
				nfo.type = smb["contractType"].getString();
				nfo.wallet_id = symbol;
				bld.push_back(VT(symbol, nfo));
			}
			need_more_time();
			res = fapi.public_request("/fapi/v1/exchangeInfo",Value());
			symbol = USDT_M_PREFIX;
			for (Value smb: res["symbols"]) {
				if (smb["status"].getString() != "TRADING") continue;
				symbol.resize(USDT_M_PREFIX.length());
				symbol.append(smb["symbol"].getString());
				MarketInfoEx nfo;
				nfo.asset_symbol = smb["baseAsset"].getString();
				nfo.currency_symbol = smb["quoteAsset"].getString();
				nfo.currency_step = std::pow(10,-smb["pricePrecision"].getNumber());
				nfo.asset_step = std::pow(10, -smb["quantityPrecision"].getNumber());
				nfo.feeScheme = currency;
				nfo.fees = getFees(symbol);
				nfo.min_size = nfo.asset_step;
				nfo.min_volume = 0;
				nfo.size_precision = smb["baseAssetPrecision"].getUInt();
				nfo.quote_precision = smb["quotePrecision"].getUInt();
				for (Value f: smb["filters"]) {
					auto ft = f["filterType"].getString();
					if (ft == "LOT_SIZE") {
						nfo.min_size = f["minQty"].getNumber()*nfo.asset_step;
					} else if (ft == "PRICE_FILTER") {
						nfo.currency_step = f["tickSize"].getNumber();
					} else if (ft == "MIN_NOTIONAL") {
						nfo.min_volume = f["notional"].getNumber();
					}
				}
				nfo.leverage = 20;
				nfo.invert_price = false;
				nfo.cat = Category::usdt_m;
				nfo.label = nfo.asset_symbol+"/"+nfo.currency_symbol;
				nfo.type = "PERPETUAL";
				nfo.wallet_id = "usdt-m";
				bld.push_back(VT(symbol, nfo));
			}

			symbols = Symbols(std::move(bld));
			symbolsExpire = now + std::chrono::minutes(15);
			need_more_time();
	}
}

void Interface::onInit() {
	idsrc = now();
}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	initSymbols();

	auto iter = symbols.find(pair);
	if (iter == symbols.end())
		throw std::runtime_error("Unknown trading pair symbol");
	MarketInfo res = iter->second;
	if (dapi_isSymbol(pair)) {
		auto lev = dapi_getLeverage(remove_prefix(pair));
		if (lev.defined()) res.leverage = lev.getNumber();
	}
	if (fapi_isSymbol(pair)) {
		auto lev = fapi_getLeverage(remove_prefix(pair));
		if (lev.defined()) res.leverage = lev.getNumber();
	}
	return res;
}

bool Interface::dapi_isSymbol(const std::string_view &pair) {
	return pair.substr(0, COIN_M_PREFIX.length()) == COIN_M_PREFIX;
}

inline double Interface::getFees(const std::string_view &pair) {
	if (px.hasKey()) {
		if (dapi_isSymbol(pair)) {
			return dapi_getFees();
		} else if (fapi_isSymbol(pair)) {
			return fapi_getFees();
		} else {
			 if (!feeInfo.defined()) {
				 updateBalCache();
			 }
			 return feeInfo.getNumber();
		}
	} else {
		if (dapi_isSymbol(pair)) {
			return 0.00015;
		} else if (fapi_isSymbol(pair)) {
			return 0.0002;
		} else {
			return 0.001;
		}
	}
}



inline void Interface::onLoadApiKey(json::Value keyData) {
	px.privKey = keyData["privKey"].getString();
	px.pubKey = keyData["pubKey"].getString();
	dapi.privKey = px.privKey;
	dapi.pubKey = px.pubKey;
	fapi.privKey = px.privKey;
	fapi.pubKey = px.pubKey;
	symbols.clear();
}

inline Value Interface::generateOrderId(Value clientId) {
	std::ostringstream stream;
	Value(json::array,{idsrc++, clientId.stripKey()},false).serializeBinary([&](char c){
		stream.put(c);
	});
	std::string s = stream.str();
	auto bs= json::map_str2bin(s);
	return Value((String({
		"mmbot",base64url->encodeBinaryValue(bs).getString()
	})));
}


Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"binance",
		"Binance",
		"https://www.binance.com/en/register?ref=37092760",
		"2.1",
		"Copyright (c) 2019 Ondřej Novák\n\n"

"Permission is hereby granted, free of charge, to any person "
"obtaining a copy of this software and associated documentation "
"files (the \"Software\"), to deal in the Software without "
"restriction, including without limitation the rights to use, "
"copy, modify, merge, publish, distribute, sublicense, and/or sell "
"copies of the Software, and to permit persons to whom the "
"Software is furnished to do so, subject to the following "
"conditions: "
"\n\n"
"The above copyright notice and this permission notice shall be "
"included in all copies or substantial portions of the Software. "
"\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES "
"OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT "
"HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, "
"WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING "
"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR "
"OTHER DEALINGS IN THE SOFTWARE.",
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAABlBMVEX31lXzui274TLJAAAAAXRS"
"TlMAQObYZgAAAYlJREFUeNrt20FuwzAQQ9Hw/pfuku0iNQxCfQ1gLgMin4ltSR6NXo8efboSzU80"
"P9H8RPMTxa8Uv1L8SvErxa8Uv1L8SvErxa8Uv1L8SvErHiCKXyl+pfiV4leKXyl+pfiV4lean7/h"
"r5Pzzt8S7Pwtwc5fE+z88wnyVjdch/hbgp2/JNj5m3cPsJn3BJt5T7CZ9wSbeU8wmvcEd8ygGnR8"
"OrxjNhXBmllNdOa/H2h/9Vbv+Dd+wIlPX7cu4fi/XJiHq33h3c23bvcD5lsP/AFzvguMj/mpgzPE"
"PpVlMu+TeUbzupzJbN4WdMluXpa0mcz1+gD+Evib0D+GfiDSQ7GfjPx07Bckfkm2L0on86kF+MUX"
"qxcT8GpmX07967kvUPgSjS9S+TKdL1TyUi0vVvNyvd6w0Fs2etNKb9vpjUu9das3r/X2vW9gwAl8"
"E4tN8F8amVQC38xmE+h+Qt1R6ZtabQLf2GwT+OZ2m8AfcLAJ/CEXm8AfdLIJ/GE3m8AfeLQJ+KHX"
"R48+XF9VnRBZ1a2+VQAAAABJRU5ErkJggg==",true,true


	};
}

int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required storage path" << std::endl;
		return 1;
	}

	try {

		Interface ifc(argv[1]);
		ifc.dispatch();


	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

inline Value Interface::dapi_readAccount() {
	try {
		if (!dapi_account.defined()) {
			Value account = dapi.private_request(Proxy::GET, "/dapi/v1/account", Value());
			dapi_account = account;
		}
		return dapi_account;
	} catch (...) {
		return dapi_account = json::object;
	}
}

inline Value Interface::fapi_readAccount() {
	try {
		if (!fapi_account.defined()) {
			Value account = fapi.private_request(Proxy::GET, "/fapi/v2/account", Value());
			fapi_account = account;
		}
		return fapi_account;
	} catch (...) {
		return fapi_account = json::object;
	}
}

inline double Interface::dapi_getFees() {
	double fees[] = {0.00015, 0.00013, 0.00011, 0.00010};
	Value account = dapi_readAccount();
	unsigned int tier = account["feeTier"].getNumber();
	if (tier >= 4) return 0.0;
	return fees[tier];
}

inline json::Value Interface::dapi_getLeverage(const std::string_view &pair) {
	Value a = dapi_readAccount();
	Value b = a["positions"];
	Value z = b.find([&](Value itm){
		return itm["symbol"] == pair;
	});
	return z["leverage"];
}

inline double Interface::dapi_getPosition(const std::string_view &pair) {
	if (!dapi_positions.defined()) {
		dapi_positions = dapi.private_request(Proxy::GET, "/dapi/v1/positionRisk", Value());
	}
	Value z = dapi_positions.find([&](Value item){return item["symbol"].getString() == pair;});
	return -z["positionAmt"].getNumber();
}

inline double Interface::dapi_getCollateral(const std::string_view &pair) {
	Value a = dapi_readAccount();
	Value ass = a["assets"];
	Value z = ass.find([&](Value item){return item["asset"].getString() == pair;});
	return z["walletBalance"].getNumber()+z["unrealizedProfit"].getNumber();
}

inline json::Value Interface::getMarkets() const {
	const_cast<Interface *>(this)->initSymbols();
	using Map = std::map<std::pair<std::string_view, std::string_view>, std::string_view>;
	auto loadToMap = [](const auto &map) {
		Object lst;
		Object sub;
		std::string_view p;
		for (auto &&c: map) {
			if (c.first.first != p) {
				if (!p.empty()) {
					lst.set(p, sub);
					sub.clear();
				}
				p = c.first.first ;
			}
			sub.set(c.first.second, c.second);
		}
		if (!p.empty()) {
			lst.set(p, sub);
		}
		return lst;
	};
	Object res;
	{
		Map map;
		for (auto &&v: symbols) if (v.second.cat == Category::spot) {
			map.emplace(std::pair(std::string_view(v.second.asset_symbol), std::string_view(v.second.currency_symbol)), v.first);
		}
		res.set("Spot",loadToMap(map));
	}
	{
		Map map;
		for (auto &&v: symbols) if (v.second.cat == Category::coin_m) {
			map.emplace(std::pair(std::string_view(v.second.label), std::string_view(v.second.type)), v.first);
		}
		res.set(coin_m_title,loadToMap(map));
	}
	{
		Map map;
		for (auto &&v: symbols) if (v.second.cat == Category::usdt_m) {
			map.emplace(std::pair(std::string_view(v.second.label), std::string_view(v.second.type)), v.first);
		}
		res.set(usdt_m_title,loadToMap(map));
	}



	return res;
}

inline json::Value Interface::setSettings(json::Value v) {
	feesInBnb = v["bnbfee"].getString() == "yes";
	symbols.clear();
	return v;
}

inline json::Value Interface::getSettings(const std::string_view &) const {
	return {
		Object({
			{"name","bnbfee"},
			{"label","Fees paid in BNB"},
			{"type","enum"},
			{"options",Object({
					{"yes","Enabled"},
					{"no","Disabled"}})},
			{"default",feesInBnb?"yes":"no"}})
	};
}

inline void Interface::restoreSettings(json::Value v) {
	setSettings(v);
}

bool Interface::fapi_isSymbol(const std::string_view &pair) {
	return pair.substr(0, USDT_M_PREFIX.length()) == USDT_M_PREFIX;
}

inline double Interface::fapi_getPosition(const std::string_view &pair) {
	if (!fapi_positions.defined()) {
		fapi_positions = fapi.private_request(Proxy::GET, "/fapi/v2/positionRisk", Value());
	}
	Value z = fapi_positions.find([&](Value item){return item["symbol"].getString() == pair;});
	return z["positionAmt"].getNumber();
}

double Interface::fapi_getFees() {
	double fees[] = {0.00020, 0.00016, 0.00012, 0.00010, 0.00008, 0.00006, 0.00004, 0.00002};
	Value account = fapi_readAccount();
	unsigned int tier = account["feeTier"].getNumber();
	if (tier >= 8) return 0.0;
	return fees[tier];
}

double Interface::fapi_getCollateral(const std::string_view &currency) {
	Value account = fapi_readAccount();
	Value assets = account["assets"];
	Value srch = assets.find([&](Value v){
		return v["asset"].getString() == currency;
	});
	return srch["walletBalance"].getNumber()+account["unrealizedProfit"].getNumber();
}

Value Interface::getWallet_direct()  {
	updateBalCache();
	Object out;
	out.set("spot", balanceCache["balances"].map([&](Value x){
		double n = x["free"].getNumber()+x["locked"].getNumber();
		if (n) return Value(n); else return Value();
	}));
	try {
		Object fut;
		fut.set("USDT", fapi_getCollateral("USDT"));
		fut.set("BUSD", fapi_getCollateral("BUSD"));
		Value dacc = dapi_readAccount();
		for (Value x:dacc["assets"]) {
			double n = x["walletBalance"].getNumber()+x["unrealizedProfit"].getNumber();
			if (n) {
				fut.set(x["asset"].getString(), n);
			}
		}
		out.set("futures", fut);

		fapi_getPosition("");

		Object poss;
		for (Value x:fapi_positions) {
			double n = x["positionAmt"].getNumber();
			if (n) {
				poss.set(x["symbol"].getString(), n);
			}
		}



		dapi_getPosition("");
		for (Value x:dapi_positions) {
			double n = x["positionAmt"].getNumber();
			if (n) {
				poss.set(x["symbol"].getString(), n);
			}
		}
		out.set("positions", poss);
	} catch (...) {
		//empty
	}
	return out;

}

bool Interface::isMatchedPair(const MarketInfo &nfo, const std::string_view &asset, const std::string_view &currency) {
	return (nfo.asset_symbol == asset && nfo.currency_symbol == currency)
			|| (nfo.asset_symbol == currency && nfo.currency_symbol == asset);
}

bool Interface::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	initSymbols();
	auto iter = std::find_if(symbols.begin(), symbols.end(), [&](const auto &nfo) {
		return isMatchedPair(nfo.second, asset, currency);
	});
	return iter != symbols.end();
}

std::uint64_t Interface::downloadMinuteData(
		const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, std::uint64_t time_from,
		std::uint64_t time_to, HistData &xdata) {
	std::uint64_t adj_time_from = time_to-1000*300000; //LIMIT 1000 per 5 minute
	time_from = std::max(adj_time_from, time_from);
	auto limit = (time_to-time_from-1)/300000;
	if (limit <= 0) return 0;
	MinuteData data;
	initSymbols();
	auto iter = symbols.find(hint_pair);
	if (iter == symbols.end() || !isMatchedPair(iter->second, asset, currency)) {
		iter = std::find_if(symbols.begin(), symbols.end(), [&](const auto &nfo) {
			return isMatchedPair(nfo.second, asset, currency);
		});
		if (iter == symbols.end()) return 0;
	}
	Value tmp;
	auto smb = remove_prefix(iter->first);
	switch (iter->second.cat) {
	case Category::spot:
		tmp = px.public_request("/api/v3/klines",Object{{"symbol",smb},{"interval","5m"},{"limit",1000},{"startTime",time_from},{"endTime",time_to}});
		break;
	case Category::usdt_m:
		tmp = fapi.public_request("/fapi/v1/klines",Object{{"symbol",smb},{"interval","5m"},{"limit",1000},{"startTime",time_from},{"endTime",time_to}});
		break;
	case Category::coin_m:
		tmp = dapi.public_request("/dapi/v1/klines",Object{{"symbol",smb},{"interval","5m"},{"limit",1000},{"startTime",time_from},{"endTime",time_to}});
		break;
	}
	double prev=0;
	auto insert_val = [&,inv=iter->second.currency_symbol == asset](double n){
		if (prev) {
			double z = n/prev;
			if (z < 0.8 || z>1.4) {
				std::cerr << "Value filtered: " << n << " - " << prev << std::endl;
				n = prev;
			}
		}
		prev = n;
		if (inv) n=1/n;
		data.push_back(n);
	};
	for (Value v: tmp) {
		auto tm = v[0].getUIntLong();
		if (tm >= time_to) break;
		double o = v[1].getNumber();
		double h = v[2].getNumber();
		double l = v[3].getNumber();
		double c = v[4].getNumber();
		double m = std::sqrt(h*l);
		insert_val(o);
		insert_val(h);
		insert_val(m);
		insert_val(l);
		insert_val(c);
	}
	if (data.empty()) return 0;
	xdata = std::move(data);
	return tmp[0][0].getUIntLong();
}

json::Value Interface::fapi_getLeverage(const std::string_view &pair) {
	Value a = fapi_readAccount();
	Value b = a["positions"];
	Value z = b.find([&](Value itm){
		return itm["symbol"] == pair;
	});
	return z["leverage"];
}
