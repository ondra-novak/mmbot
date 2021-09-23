#include "kucoin.h"

#include <openssl/hmac.h>
#include <sstream>
#include <random>
#include <thread>

#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <imtjson/serializer.h>
#include <imtjson/binjson.tcc>
#include <imtjson/operations.h>
#include <shared/toString.h>
#include <simpleServer/urlencode.h>
#include "../../shared/logOutput.h"

using ondra_shared::logDebug;

using namespace json;

static std::string_view favicon(
		"iVBORw0KGgoAAAANSUhEUgAAAIAAAACABAMAAAAxEHz4AAAAG1BMVEUhAAAXp4oZqIsbqYwdqo0g"
		"q44hrI8jrpAlr5HbXGoxAAAAAXRSTlMAQObYZgAAAg9JREFUaN7tmTtuxDAMRBcwMEfIEVK72vMQ"
		"MMD7HyEpYmQl8atJulUpyoNn/UiRj8e71RpUL/L778Z+r+e2gP40EkBVSIBdBPwKCAmwh4BXASEB"
		"IgQ4VowCEoJeKYCP4OljFpB4plIAD+G2nhmAg+BuVtUaAhwrLAFpCKgWEZ62EbaAlM+bahXBtsET"
		"kKKAahnBNMEXkJKAah3BskwAnyGCJTBBHyGCYcD8wTNCMASWWQsR1n6swyOEVcBYtghh6YY1OEBY"
		"BMx9EyDMvbCH+gjzWG/jughTJ7yBo+F0BbSAOhpGAfiThZKAlnbM4MgGAUR7FrZp6NPiwfUEEF8d"
		"z/QXksvryAiQ3eBIViF3Y7HAR8ORFi5gqQaudR/mhM4tgHt4wYl5AdVRdMMyYDeCSWeSpCsg0U9X"
		"BJZpv3oC0nmxxABHHqvHADDOby5QCAJDAcmDwHpIrRsCwgrQBPwc0KvA7wN6J/JngT6N/H1A30j8"
		"nRgve0WA9gu0Z9rxjbR3puMDPkJJEdgoDXmgycaJfKRKx8p8tE6/F/gXC/1m8hBQfrU5CA3vZyKg"
		"438thM7b2UJA6/VuIPTyBysCmhmMBaGbQ+GzOHQeic9k0bk0PpvX8Hh0RpPOqf5dVnc3r/zwBKqZ"
		"bT63Tmf3/VpWOfBiKxx8jeX/qjx0nYmvdNG1Nr7aR9cbb4RtAL7myld93y1uX1kIUAloS5SdAAAA"
		"AElFTkSuQmCC"
);

static std::string_view licence(
		R"mit(Copyright (c) 2019 Ondřej Novák

		Permission is hereby granted, free of charge, to any person
		obtaining a copy of this software and associated documentation
		files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use,
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
		OTHER DEALINGS IN THE SOFTWARE.)mit"
);

static Value apiKeyFmt ({
		Object{{"name","passphrase"},{"label","API Passphrase"},{"type","string"}},
		Object({
			{"type","label"},
			{"label","Paste the randomly generated passphrase into the API key creation request form using the Copy and Paste function."}
		}),
		Object{{"name","key"},{"label","Key"},{"type","string"}},
		Object{{"name","secret"},{"label","Secret"},{"type","string"}}
});

KucoinIFC::KucoinIFC(const std::string &cfg_file)
	:AbstractBrokerAPI(cfg_file, apiKeyFmt)
	,api(simpleServer::HttpClient("mmbot (+https://www.mmbot.trade)",simpleServer::newHttpsProvider(), 0, simpleServer::newCachedDNSProvider(15)),"https://api.kucoin.com")
{
}

IStockApi::BrokerInfo KucoinIFC::getBrokerInfo() {
	return BrokerInfo {
		hasKey(),
		"kucoin",
		"KuCoin",
		"https://www.kucoin.com/",
		"1.0",
		std::string(licence),
		std::string(favicon),
		false,
		true
	};
}


std::vector<std::string> KucoinIFC::getAllPairs() {
	updateSymbols();
	std::vector<std::string> out;
	out.reserve(symbolMap.size());
	for (const auto &x: symbolMap) out.push_back(x.first);
	return out;
}

bool KucoinIFC::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	updateSymbols();
	auto iter = std::find_if(symbolMap.begin(), symbolMap.end(), [&](const auto &x){
		return x.second.currency_symbol == currency && x.second.asset_symbol == asset;
	});
	return iter != symbolMap.end();
}

IStockApi::MarketInfo KucoinIFC::getMarketInfo(const std::string_view &pair) {
	const auto &s = findSymbol(pair);
	if (s.fees < 0) updateSymbolFees(pair);
	return s;

}

AbstractBrokerAPI* KucoinIFC::createSubaccount(
		const std::string &secure_storage_path) {
	return new KucoinIFC(secure_storage_path);
}

void KucoinIFC::onLoadApiKey(json::Value keyData) {
	auto passphrase = keyData["passphrase"].getString();
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();



	api_passphrase.clear();
	if (!passphrase.empty()) {
		unsigned char sign[256];
		unsigned int signLen(sizeof(sign));
		HMAC(EVP_sha256(), api_secret.data(), api_secret.length(),
				reinterpret_cast<const unsigned char *>(passphrase.data()), passphrase.length(), sign, &signLen);
		base64->encodeBinaryValue(json::BinaryView(sign, signLen), [&](std::string_view x){
			api_passphrase.append(x);
		});
	}

	symbolExpires = api.now();

}


uint64_t KucoinIFC::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<IHistoryDataSource::OHLC> &data) {
	updateSymbols();
	auto iter = symbolMap.find(hint_pair);
	if (iter == symbolMap.end()) {
		iter = std::find_if(symbolMap.begin(), symbolMap.end(), [&](const auto &x){
			return x.second.currency_symbol == currency && x.second.asset_symbol == asset;
		});
	}
	time_from/=1000;
	time_to/=1000;
	if (iter != symbolMap.end()) {
		Value r = publicGET("/api/v1/market/candles", Object{
			{"type","5min"},
			{"symbol",iter->first},
			{"startAt",time_from},
			{"endAt",time_to},
		});
		std::uint64_t minTime = time_to;
		for (Value rw: r) {
			std::uint64_t tm = rw[0].getUIntLong();
			if (tm>=time_from && tm < time_to) {
				double o = rw[1].getNumber();
				double c = rw[2].getNumber();
				double h = rw[3].getNumber();
				double l = rw[4].getNumber();
				double m = std::sqrt(h*l);
				data.push_back({o,o,o,o});
				data.push_back({l,l,l,l});
				data.push_back({m,m,m,m});
				data.push_back({h,h,h,h});
				data.push_back({c,c,c,c});
				minTime= std::min(minTime, tm);
			}
		}
		if (data.empty()) {
			return 0;
		} else {
			return minTime*1000;
		}

	} else {
		return 0;
	}

	return 0;
}

json::Value KucoinIFC::getMarkets() const {
	updateSymbols();
	std::map<std::string_view, Object> smap;
	for(const auto &x: symbolMap) {
		smap[x.second.asset_symbol].set(x.second.currency_symbol, x.first);
	}
	return Object{
		{"Spot",Value(object, smap.begin(), smap.end(),[](const auto &x){
			return Value(x.first, x.second);
		})}
	};
}

double KucoinIFC::getBalance(const std::string_view &symb, const std::string_view &) {
	updateBalances();
	auto iter = balanceMap.find(symb);
	if (iter == balanceMap.end()) return 0;
	return iter->second;
}

void KucoinIFC::onInit() {

}

IStockApi::TradesSync KucoinIFC::syncTrades(json::Value lastId, const std::string_view &pair) {
	const MarketInfoEx &minfo = findSymbol(pair);

	Array mostIDS;
	std::uint64_t mostTime = 0;
	auto findMostTime = [&](Value fills) {
		for (Value f: fills) {
			std::uint64_t t = f["createdAt"].getUIntLong();
			if (mostTime <=t) {
				if (mostTime < t) mostIDS.clear();
				mostIDS.push_back(f["tradeId"]);
				mostTime = t;
			}
		}
	};


	if (lastId[0].getUIntLong()>0) {

		Value fills = privateGET("/api/v1/fills", Object{
			{"symbol",pair},
			{"pageSize",500},
			{"startAt",lastId[0]},
			{"endAt",std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count()}
		})["items"];
		if (fills.empty()) {
			return {{},lastId};
		}
		findMostTime(fills);
		Value ffils = fills.filter([&](Value r){
			return lastId[1].indexOf(r["tradeId"]) == Value::npos;
		});
		if (!fills.empty() && ffils.empty()) {
			return {{},{mostTime+1, mostIDS}};
		}
		return {
			mapJSON(fills, [&](Value rw){
				double size = rw["size"].getNumber()*(rw["side"].getString() == "buy"?1:-1);
				double price = rw["price"].getNumber();
				double fee = rw["fee"].getNumber();
				double eff_price = price;
				double eff_size = size;
				std::string_view feeCurrency = rw["feeCurrency"].getString();
				if (feeCurrency == minfo.currency_symbol) {
					eff_price = (price * size + fee)/size;
				} else if (feeCurrency == minfo.asset_symbol) {
					eff_size = size - fee;
				}
				return Trade{
					rw["tradeId"],
					rw["createdAt"].getUIntLong(),
					size, price,
					eff_size, eff_price
				};

			}, TradeHistory()),
			{mostTime, mostIDS}
		};




	} else {
		Value fills = privateGET("/api/v1/fills", Object{
			{"symbol",pair}
		})["items"];
		findMostTime(fills);
		return TradesSync{{},{mostTime, mostIDS}};

	}

}

bool KucoinIFC::reset() {
	balanceMap.clear();
	return true;
}

IStockApi::Orders KucoinIFC::getOpenOrders(const std::string_view &pair) {
	Value res = privateGET("/api/v1/orders",Object{
		{"status","active"},
		{"pageSize",500},
		{"symbol", pair}
	})["items"];
	return mapJSON(res, [&](Value row){
		return Order {
			row["id"].getString(),
			parseOid(row["clientOid"]),
			(row["size"].getNumber() - row["dealSize"].getNumber())*(row["side"].getString()=="buy"?1:-1),
			row["price"].getNumber()
		};
	}, IStockApi::Orders());
}

json::Value KucoinIFC::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {
	if (replaceId.defined()) {
		std::string orderURI("/api/v1/orders/");
		orderURI.append(replaceId.getString());
		privateDELETE(orderURI, Value());

		do {
			Value v = privateGET(orderURI, Value());
			if (v["isActive"].getBool() == false) {
				double remain = v["size"].getNumber() - v["dealSize"].getNumber();
				if (remain > replaceSize*0.95) break;
				else return nullptr;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		} while(true);
	}
	if (size) {
		Value c = privatePOST("/api/v1/orders", json::Object{
			{"clientOid",generateOid(clientId)},
			{"side",size<0?"sell":"buy"},
			{"symbol", pair},
			{"type","limit"},
			{"stp","DC"},
			{"price", price},
			{"size",std::abs(size)},
			{"postOnly",true},
		});
		return c["orderId"];

	}

	return nullptr;
}


IBrokerControl::AllWallets KucoinIFC::getWallet() {
	updateBalances();
	Wallet w;
	w.walletId="spot";
	for (const auto &x : balanceMap) {
		w.wallet.push_back({x.first, x.second});
	}
	return {w};
}

IStockApi::Ticker KucoinIFC::getTicker(const std::string_view &pair) {
	json::Value res = publicGET("/api/v1/market/orderbook/level1",Object{{"symbol",pair}});
	return IStockApi::Ticker {
		res["bestBid"].getNumber(),
		res["bestAsk"].getNumber(),
		res["price"].getNumber(),
		res["time"].getUIntLong()
	};
}

json::Value KucoinIFC::getApiKeyFields() const {
	std::string randomPwd;
	randomPwd.reserve(32);
	std::random_device rnd;
	std::uniform_int_distribution<int> rnd_code(0,61);
	for (int i = 0; i<32; i++) {
		int code = rnd_code(rnd);
		char c = code < 10?'0'+code:code<36?'A'+code-10:'a'+code-36;
		randomPwd.push_back(c);
	}
	Value flds = AbstractBrokerAPI::getApiKeyFields();
	Value pswd = flds[0];
	pswd.setItems({
		{"default",randomPwd}
	});
	return flds.replace(0, pswd);
}

Value KucoinIFC::publicGET(const std::string_view &uri, Value query) const {
	try {
		return processResponse(api.GET(buildUri(uri, query)));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

const std::string& KucoinIFC::buildUri(const std::string_view &uri, Value query) const {
	uriBuffer.clear();
	uriBuffer.append(uri);
	char c='?';
	for (Value v: query) {
		uriBuffer.push_back(c);
		uriBuffer.append(v.getKey());
		uriBuffer.push_back('=');
		simpleServer::urlEncoder([&](char x){uriBuffer.push_back(x);})(v.toString());
		c = '&';
	}
	return uriBuffer;
}

bool KucoinIFC::hasKey() const {
	return !(api_passphrase.empty() || api_key.empty() || api_secret.empty());
}

Value KucoinIFC::privateGET(const std::string_view &uri, Value query)  const{
	try {
		std::string fulluri = buildUri(uri, query);
		return processResponse(api.GET(fulluri,signRequest("GET", fulluri, Value())));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

Value KucoinIFC::privatePOST(const std::string_view &uri, Value args) const {
	try {
		return processResponse(api.POST(uri, args, signRequest("POST", uri, args)));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

Value KucoinIFC::privateDELETE(const std::string_view &uri, Value query) const {
	try {
		std::string fulluri = buildUri(uri, query);
		return processResponse(api.DELETE(fulluri,Value(),signRequest("DELETE", fulluri, Value())));
	} catch (const HTTPJson::UnknownStatusException &e) {
		processError(e);
		throw;
	}
}

Value KucoinIFC::signRequest(const std::string_view &method, const std::string_view &function, json::Value args) const {
	std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(api.now().time_since_epoch()).count());
	std::string digest = timestamp;
	digest.append(method);
	digest.append(function);
	if (args.defined()) args.serialize([&](char c){digest.push_back(c);});
	unsigned char sign[256];
	unsigned int signLen(sizeof(sign));
	HMAC(EVP_sha256(), api_secret.data(), api_secret.length(),
			reinterpret_cast<const unsigned char *>(digest.data()), digest.length(), sign, &signLen);
	Value s =  Object{
		{"KC-API-KEY",api_key},
		{"KC-API-SIGN",Value(json::BinaryView(sign, signLen),base64)},
		{"KC-API-TIMESTAMP",timestamp},
		{"KC-API-PASSPHRASE",api_passphrase},
		{"KC-API-KEY-VERSION",2}
	};
	logDebug("SIGN: $1", s.toString().str());
	return s;

}

void KucoinIFC::processError(const HTTPJson::UnknownStatusException &e) const {
	std::ostringstream buff;
	buff << e.getStatusCode() << " " << e.getStatusMessage();
	try {
		auto s = e.response.getBody();
		json::Value error = json::Value::parse(s);
		buff <<  " - " << error["code"].getUInt() << " " << error["msg"].getString();
	} catch (...) {

	}
	throw std::runtime_error(buff.str());
}

double KucoinIFC::getFees(const std::string_view &pair) {
	return getMarketInfo(pair).fees;
}

void KucoinIFC::updateSymbols() const {
	auto now = api.now();
	if (symbolExpires <= now) {

		Value symblst = publicGET("/api/v1/symbols", Value());
		SymbolMap::Set::VecT smap;

		for (Value s: symblst) {
			auto symbol = s["symbol"].getString();
			MarketInfoEx nfo;
			nfo.asset_step = s["baseIncrement"].getNumber();
			nfo.currency_step = s["priceIncrement"].getNumber();
			nfo.asset_symbol= s["baseCurrency"].getString();
			nfo.currency_symbol = s["quoteCurrency"].getString();
			nfo.feeScheme = currency;
			nfo.fees = -1;
			nfo.invert_price = false;
			nfo.leverage = 0;
			nfo.min_size = s["baseMinSize"].getNumber();
			nfo.min_volume = s["quoteMinSize"].getNumber();
			nfo.private_chart = false;
			nfo.simulator = false;
			nfo.wallet_id = "spot";
			smap.emplace_back(std::string(symbol), std::move(nfo));

		}


		symbolMap = SymbolMap(std::move(smap));

		symbolExpires = now +std::chrono::hours(1);
	}
}

const KucoinIFC::MarketInfoEx& KucoinIFC::findSymbol(const std::string_view &name) const {
	updateSymbols();
	auto iter = symbolMap.find(name);
	if (iter == symbolMap.end()) throw std::runtime_error("Unknown symbol");
	return iter->second;
}

void KucoinIFC::updateSymbolFees(const std::string_view &name)  {
	updateSymbols();
	auto iter = symbolMap.find(name);
	if (iter == symbolMap.end()) return;
	if (hasKey()) {
		Value x = privateGET("/api/v1/trade-fees", Object{{"symbols", name}});
		iter->second.fees = x[0]["makerFeeRate"].getNumber();
	} else {
		iter->second.fees = 0.001;
	}
}

void KucoinIFC::updateBalances() {
	if (balanceMap.empty()) {
		BalanceMap::Set::VecT b;
		Value res = privateGET("/api/v1/accounts", Object{{"type","trade"}});
		for(Value r:res) {
			std::string_view cur = r["currency"].getString();
			double balance = r["balance"].getNumber();
			b.emplace_back(std::string(cur), balance);
		}
		if (b.empty()) b.emplace_back(std::string(""),0.0);
		balanceMap = BalanceMap(std::move(b));
	}
}

json::Value KucoinIFC::generateOid(Value clientId) {
		auto id = nextId++;
		Value ctx = {id, clientId.stripKey()};
		std::basic_string<unsigned char> oidBuff;
		ctx.serializeBinary([&](char c){oidBuff.push_back(c);});
		return json::Value(json::BinaryView(oidBuff), base64url);

}

json::Value KucoinIFC::processResponse(json::Value v) const {
	if (v["data"].defined()) return v["data"];
	std::ostringstream buff;
	buff <<  v["code"].getUInt() << " " << v["msg"].getString();
	throw std::runtime_error(buff.str());

}

Value KucoinIFC::parseOid(json::Value  oid) {
	if (!oid.defined()) return json::Value();
	Binary b = oid.getBinary(base64url);
	if (b.empty()) return json::Value();
	std::size_t c = 0;
	try {
		Value v = Value::parseBinary([&]()->int{
			if (c < b.size()) {return b[c++];} else throw std::runtime_error("invalid oid");
		}, base64url);
		unsigned int id = v[0].getUInt();
		if (id > nextId) nextId = id;
		return v[1];
	} catch (...) {
		return json::Value();
	}
}
