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
#include "../main/istockapi.h"
#include <cmath>
#include <ctime>

#include "../brokers/api.h"
#include <imtjson/stringValue.h>
#include "../shared/linear_map.h"
#include "../shared/iterator_stream.h"
#include <imtjson/binary.h>
#include <imtjson/streams.h>
#include <imtjson/binjson.tcc>
#include "../main/sgn.h"

using namespace json;

static Value keyFormat = {Object
							("name","pubKey")
							("type","string")
							("label","Public key"),
						 Object
							("name","privKey")
							("type","string")
							("label","Private key")};

class Interface: public AbstractBrokerAPI {
public:
	Proxy px;


	Interface(const std::string &path):AbstractBrokerAPI(path, keyFormat) {}


	virtual double getBalance(const std::string_view & symb) override;
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
	virtual void enable_debug(bool enable) override;
	virtual BrokerInfo getBrokerInfo() override;
	virtual void onLoadApiKey(json::Value keyData) override;
	virtual void onInit() override;

	using Symbols = ondra_shared::linear_map<std::string, MarketInfo, std::less<std::string_view> > ;
	using Tickers = ondra_shared::linear_map<std::string, Ticker,  std::less<std::string_view> >;

	Value balanceCache;
	Tickers tickerCache;
	Value orderCache;
	Value feeInfo;
	std::chrono::system_clock::time_point feeInfoExpiration;
	Symbols symbols;
	using TradeMap = ondra_shared::linear_map<std::string, std::vector<Trade> > ;


	TradeMap tradeMap;
	bool needSyncTrades = true;
	std::size_t lastFromTime = -1;


	static bool tradeOrder(const Trade &a, const Trade &b);
	void updateBalCache();
	Value generateOrderId(Value clientId);

	std::intptr_t time_diff;
	std::uintptr_t idsrc;

	void initSymbols();

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

 double Interface::getBalance(const std::string_view & symb) {
	 updateBalCache();
	 Value v =balanceCache["balances"][symb];
	 if (v.defined()) return v["free"].getNumber()+v["locked"].getNumber();
	 else throw std::runtime_error("No such symbol");
}
/*

 static std::size_t parseTime(json::String date) {
	 int y,M,d,h,m;
	 float s;
	 sscanf(date.c_str(), "%d-%d-%d %d:%d:%f", &y, &M, &d, &h, &m, &s);
	 float sec;
	 float msec = std::modf(s,&sec)*1000;
	 std::tm t={0};
	 t.tm_year = y - 1900;
	 t.tm_mon = M-1;
	 t.tm_mday = d;
	 t.tm_hour = h;
	 t.tm_min = m;
	 t.tm_sec = static_cast<int>(sec);
	 std::size_t res = timegm(&t) * 1000 + static_cast<std::size_t>(msec);
	 return res;

 }
*/
 Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view & pair) {
	 initSymbols();
	 auto iter = symbols.find(pair);
	 if (iter == symbols.end())
		 throw std::runtime_error("No such symbol");

	 const MarketInfo &minfo = iter->second;

	 if (lastId.hasValue()) {


		 Value r = px.private_request(Proxy::GET,"/api/v3/myTrades", Object
				 ("fromId", lastId)
				 ("symbol", pair)
				 );

		 r = r.map([&](Value x) ->Value{
			if (x["id"] == lastId) return json::undefined;
			else return x;
		  });

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
		 if (!h.empty()) lastId = h.back().id;
		 return TradesSync{
			 h,
			 lastId
		 };
	 } else {
		 Value r = px.private_request(Proxy::GET,"/api/v3/myTrades", Object
				 ("symbol", pair));
		 Value id = r.reduce([](Value l, Value itm) {
			 Value id = itm["id"];
			 return id.getUIntLong() > l.getUIntLong()?id:l;
		 }, Value(0));
		 return TradesSync {
			 {},
			 id
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

Interface::Orders Interface::getOpenOrders(const std::string_view & pair) {
	Value resp = px.private_request(Proxy::GET,"/api/v3/openOrders", Object("symbol",pair));
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

std::vector<std::string> Interface::getAllPairs() {
	initSymbols();
 	 std::vector<std::string> res;
	 for (auto &&v: symbols) res.push_back(v.first);
	 return res;
 }

static Value number_to_decimal(double v) {
	std::ostringstream buff;
	buff.precision(8);
	buff << std::fixed << "*" << v;
	std::string s = buff.str();
	StrViewA ss(s);
	ss = ss.trim([](char c){return isspace(c) || c == '0';}).substr(1);
	if (ss.ends("."))
		ss = ss.substr(0,ss.length-1);
	return ss;
}


json::Value Interface::placeOrder(const std::string_view & pair,
		double size,
		double price,
		json::Value clientId,
		json::Value replaceId,
		double replaceSize) {

	if (replaceId.defined()) {
		Value r = px.private_request(Proxy::DELETE,"/api/v3/order",Object
				("symbol", pair)
				("orderId", replaceId));
		double remain = r["origQty"].getNumber() - r["executedQty"].getNumber();
		if (r["status"].getString() != "CANCELED"
				|| remain < std::fabs(replaceSize)*0.9999) return nullptr;
	}

	if (size == 0) return nullptr;

	Value orderId = generateOrderId(clientId);
	px.private_request(Proxy::POST,"/api/v3/order",Object
			("symbol", pair)
			("side", size<0?"SELL":"BUY")
			("type","LIMIT_MAKER")
			("newClientOrderId",orderId)
			("quantity", number_to_decimal(std::fabs(size)))
			("price", number_to_decimal(std::fabs(price)))
			("newOrderRespType","ACK"));

	return orderId;
}

bool Interface::reset() {
	balanceCache = Value();
	tickerCache.clear();
	orderCache = Value();
	needSyncTrades = true;
	return true;
}

void Interface::initSymbols() {
	if (symbols.empty()) {
		Value res = px.public_request("/api/v1/exchangeInfo",Value());

		std::uint64_t srvtm = res["serverTime"].getUIntLong();
		std::uint64_t localtm = now();
		time_diff = srvtm - localtm;

		using VT = Symbols::value_type;
		std::vector<VT> bld;
		for (Value smb: res["symbols"]) {
			MarketInfo nfo;
			nfo.asset_symbol = smb["baseAsset"].getString();
			nfo.currency_symbol = smb["quoteAsset"].getString();
			nfo.currency_step = std::pow(10,-smb["quotePrecision"].getNumber());
			nfo.asset_step = std::pow(10,-smb["baseAssetPrecision"].getNumber());
			nfo.feeScheme = income;
			nfo.min_size = 0;
			nfo.min_volume = 0;
			nfo.fees = 0;
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
			std::string symbol = smb["symbol"].getString();
			bld.push_back(VT(symbol, nfo));
		}
		symbols = Symbols(std::move(bld));
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
	return res;
}

inline double Interface::getFees(const std::string_view &pair) {
	if (px.hasKey()) {
		 if (!feeInfo.defined()) {
			 updateBalCache();
		 }
		 return feeInfo.getNumber();
	} else {
		return 0.001;
	}
}


bool Interface::tradeOrder(const Trade &a, const Trade &b) {
	std::size_t ta = a.time;
	std::size_t tb = b.time;
	if (ta < tb) return true;
	if (ta > tb) return false;
	return Value::compare(a.id,b.id) < 0;
}

inline void Interface::onLoadApiKey(json::Value keyData) {
	px.privKey = keyData["privKey"].getString();
	px.pubKey = keyData["pubKey"].getString();
}

inline Value Interface::generateOrderId(Value clientId) {
	std::ostringstream stream;
	Value(json::array,{idsrc++, clientId.stripKey()},false).serializeBinary([&](char c){
		stream.put(c);
	});
	std::string s = stream.str();
	BinaryView bs((StrViewA(s)));
	return Value((String({
		"mmbot",base64url->encodeBinaryValue(bs).getString()
	})));
}

void Interface::enable_debug(bool enable) {
	px.debug = enable;
}

Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		px.hasKey(),
		"binance",
		"Binance",
		"https://www.binance.com/",
		"1.0",
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
"R48+XF9VnRBZ1a2+VQAAAABJRU5ErkJggg=="


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

