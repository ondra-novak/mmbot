/*
 * interface.cpp
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */
#include <sstream>

#include "interface.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <cmath>
#include <map>

#include <imtjson/object.h>
#include <simpleServer/http_client.h>
#include <imtjson/operations.h>
#include <imtjson/parser.h>
#include <imtjson/string.h>
#include <imtjson/value.h>
#include <shared/stringview.h>
using json::Object;
using json::String;
using json::Value;

static const StrViewA userAgent("+https://mmbot.trade");

Interface::Interface(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path,
			{
						Object
							("name","key")
							("label","API Key")
							("type","string"),
						Object
							("name","secret")
							("label","API Key secret")
							("type","string")
			}),
	api_pub(simpleServer::HttpClient(userAgent,simpleServer::newHttpsProvider(), nullptr,simpleServer::newCachedDNSProvider(60)),
			"https://api-pub.bitfinex.com"),
	api(simpleServer::HttpClient(userAgent,simpleServer::newHttpsProvider(), nullptr,simpleServer::newCachedDNSProvider(60)),
			"https://api.bitfinex.com"),
	orderDB(secure_storage_path+".db")
{
	nonce = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*10;
	order_nonce = nonce & 0xFFFFFFF;
}


int Interface::genOrderNonce() {
	order_nonce = (order_nonce+1) & 0xFFFFFFF;
	return order_nonce;
}



std::vector<std::string> Interface::getAllPairs() {
	std::vector<std::string> res;
	auto pairs = getPairs();
	std::string margin = " (m)";
	for (auto &&p: pairs) {
		if (p.second.leverage) {
			res.push_back(p.second.symbol.c_str()+margin);
		}
		res.push_back(p.second.symbol.str());
	}
	std::sort(res.begin(), res.end());
	return res;
}

IStockApi::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	bool margin = isMarginPair(pair);
	StrViewA name = stripMargin(pair);
	const PairList &pls = getPairs();
	auto itr = pls.find(name);
	if (itr == pls.end()) throw std::runtime_error("Unknown symbol");
	auto csit = curStep.find(pair);
	if (csit == curStep.end()) {
		auto tk = getTicker(pair);
		double step = std::pow(10,std::round(std::log10(tk.last))-5);
		csit = curStep.emplace(std::string(pair),step).first;
	}

	double minstep = std::pow(10,std::floor(std::log10(itr->second.min_size))-4);

	return MarketInfo{
		itr->second.asset.str(), /* asset_symbol; */
		itr->second.currency.str(), /* std::string currency_symbol;*/
		minstep, /* double asset_step;*/
		csit->second,                    /*double currency_step;*/
		itr->second.min_size, /* double min_size;*/
		0,				 	   /* double min_volume; */
		getFees(pair),  	/*double fees;*/
		margin?currency:income,				/* FeeScheme feeScheme = currency;*/
		margin?itr->second.leverage:0, /*double leverage = 0;*/
		false,std::string(),false,false,margin?"margin":"exchange"
	};
}


AbstractBrokerAPI* Interface::createSubaccount(
				const std::string &secure_storage_path) {
	return new Interface(secure_storage_path);
}


IStockApi::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo{
		hasKey(),
		"bitfinex",
		"Bitfinex",
		"https://www.bitfinex.com/?refcode=QoenTafCw",
		"1.0",
		R"mit(Copyright (c) 2019 Ondřej Novák

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software withourestriction, including without limitation the rights to use,
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
"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",
false,true
	};
}

void Interface::onLoadApiKey(json::Value keyData) {
	this->keyId=keyData["key"].getString();
	this->keySecret=keyData["secret"].getString();
}


static auto findConversion(const PairList &pairs, const StrViewA from, const StrViewA to) {
	for (auto &&k: pairs) {
		if (k.second.asset == from && k.second.currency == to) {
			return k.second.symbol;
		}
	}
	return PairInfo::string();
}


double Interface::getBalance(const std::string_view &symb, const std::string_view &pair) {
	if (isMarginPair(pair)) {
		auto pairs = getPairs();
		auto iter = pairs.find(stripMargin(pair));
		if (iter == pairs.end()) return 0;
		if (iter->second.asset.str() == symb) {
			if (!positions.has_value()) {
				auto data = signedPOST("/v2/auth/r/positions", json::object);
				positions = readPositions(data);
			}
			auto iter2 = positions->find(StrViewA(iter->second.tsymbol));
			if (iter2 == positions->end()) return 0;
			return iter2->second;
		} else {
			if (!marginBalance.has_value()) {
				auto data = signedPOST("/v2/auth/r/info/margin/sym_all", json::object);
				marginBalance = readMarginBalance(data);
			}
			auto iter2 = marginBalance->find(StrViewA(iter->second.tsymbol));
			if (iter2 == marginBalance->end()) return 0;
			if (iter->second.currency != "USD") {
				auto conv = findConversion(pairs, iter->second.asset, "USD");
				if (conv.empty()) return 0;
				auto tk = getTicker(conv.str());
				double assets = iter2->second / tk.last;
				tk = getTicker(pair);
				double avail = assets * tk.last;
				return avail/iter->second.leverage;
			} else {
				return iter2->second/iter->second.leverage;
			}
		}
	} else {
		if (!wallet.has_value()) {
			auto data = signedPOST("/v2/auth/r/wallets", json::object);
			wallet = readWallet(data);
		}
		auto iter = wallet->find(StrViewA(symb));
		if (iter == wallet->end()) return 0;
		return iter->second;
	}

}


IStockApi::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	const auto &pairs = getPairs();
	auto piter = pairs.find(stripMargin(pair));
	if (piter == pairs.end()) throw std::runtime_error("unknown symbol");
	const PairInfo &pinfo = piter->second;
	std::string path = "/v2/auth/r/trades/";
	path.append(pinfo.tsymbol.str());
	path.append("/hist");
	int count = 5;
	if (lastId[0].type() == json::array) lastId = Value();
	while (true) {
		int cols = 0;
		TradesSync out;
		Value data = signedPOST(path,Object("sort",-1)("limit",count));
		Value anchor = lastId;
		bool m = isMarginPair(pair);
		Value flt = data.filter([&](Value x){
			if (anchor.indexOf(x[0])!=Value::npos) {
				cols++;
				return false;
			}
			return x[6].getString().startsWith("EXCHANGE") != m;
		});
		if (lastId.hasValue() && cols<1) {
			count*=2;
			if (count < 2500) continue;
		}
		out.lastId = data.map([](Value x){return x[0];});
		std::string spair(pair);
		auto fiter = fees.find(spair);
		double lastFees =fiter == fees.end()?0.002:fiter->second;
		for (Value x: flt) {
			double price = x[5].getNumber();
			double size = x[4].getNumber();
			StrViewA feecur = x[10].getString();
			double eff_price = price;
			double eff_size = size;
			double fee = x[9].getNumber();
			lastFees = (lastFees+getFeeFromTrade(x, pinfo))/2.0;
			if (feecur == pinfo.asset && !m) {
				eff_size = size + fee;
				eff_price = size * price /eff_size;
			} else {
				eff_price = price + lastFees*price*(size>0?1:-1);
			}
			if (std::isfinite(eff_price)) {
				out.trades.push_back({
					x[0],x[2].getUIntLong(),size,price,eff_size,eff_price
				});
			}
		}
		if (!out.trades.empty()) {
			fees[std::string(pair)] = lastFees;
		}
		if (!lastId.hasValue()) {
			out.trades.clear();
		}
		else {
			std::sort(out.trades.begin(), out.trades.end(), [](const auto &a, const auto &b){
				return Value::compare(a.id, b.id) < 0;
			});
		}




		return out;
	}
}

void Interface::onInit() {
}

bool Interface::reset() {
	needUpdateTickers = true;
	marginBalance.reset();
	wallet.reset();
	positions.reset();
	return true;
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {
	const auto &pairs = getPairs();
	auto iter = pairs.find(stripMargin(pair));
	if (iter == pairs.end()) return {};

	String path ({"/v2/auth/r/orders/",iter->second.tsymbol});
	auto data = signedPOST(path.str(), json::object);
	bool m = isMarginPair(pair);
	Orders out;
	for (Value v: data) {
		if ((m && v[8].getString() == "LIMIT") || (!m && v[8].getString() == "EXCHANGE LIMIT")) {
			int cid = v[2].getInt();
			Value clientId = orderDB.get(cid);
			out.push_back({
				v[0],
				clientId,
				v[6].getNumber(),
				v[16].getNumber()
			});
		}
	}
	return out;





}

std::string numberToFixed(double numb, int fx) {
	std::ostringstream str;
	str.precision(fx);
	str.setf(std::ios_base::fixed);
	str << numb;
	return str.str();
}

json::Value Interface::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	try {
		if (replaceId.hasValue()) {
			if (size == 0 || replaceSize == 0) {
				Value resp = signedPOST("/v2/auth/w/order/cancel",Object("id", replaceId));
				Value orderDetail = resp[4];
				Value amount = orderDetail[6];
				double remain = std::abs(amount.getNumber());
				double repsz = std::abs(replaceSize);
				if (remain < repsz*0.99) return nullptr;
			} else {
				double dir = size < 0?-1:1;
				double oldsize = replaceSize * dir;
				double delta = size - oldsize;
				auto strdelta = numberToFixed(delta,8);
				Value resp = signedPOST("/v2/auth/w/order/update",Object("id", replaceId)
															   ("price", numberToFixed(price,8))
															   ("delta", strdelta == "0.00000000"?Value():Value(strdelta)));
				return resp[4][0];
			}
		}
		if (size) {
			const auto &pairs = getPairs();
			auto  iter = pairs.find(stripMargin(pair));
			if (iter == pairs.end()) throw std::runtime_error("unknown symbol");
			bool m = isMarginPair(pair);
			auto tpair = iter->second.tsymbol;
			int cid = genOrderNonce();
			orderDB.store(cid, clientId);
			Value resp = signedPOST("/v2/auth/w/order/submit",Object
					("cid", cid)
					("type", m?"LIMIT":"EXCHANGE LIMIT")
					("symbol", tpair)
					("price",numberToFixed(price,8))
					("amount",numberToFixed(size,8))
					("flags", 4096)
					("meta", Object("aff_code","QoenTafCw")));
			return resp[4][0][0];
		} else {
			return nullptr;
		}
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v.toString().str());
		} catch (...) {
			throw;
		}
	}
}

double Interface::getFees(const std::string_view &pair) {
	auto iter = fees.find(pair);
	if (iter == fees.end()) {
		syncTrades(nullptr, pair);
		iter = fees.find(pair);
	}
	if (iter == fees.end())	return 0.001;
	return std::min(iter->second,0.002);
}


IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {
	const auto &p = getPairs();
	auto piter = p.find(stripMargin(pair));
	if (piter == p.end()) throw std::runtime_error("unknown pair");
	if (needUpdateTickers) updateTickers();
	auto iter = tickers.find(std::string_view(piter->second.tsymbol.str()));
	if (iter == tickers.end()) {

		std::string req("/v2/tickers?symbols=");
		req.append(piter->second.tsymbol.str());
		Value v = publicGET(req);
		updateTicker(v[0]);
		iter = tickers.find(std::string_view(piter->second.tsymbol.str()));
		if (iter == tickers.end()) {
			throw std::runtime_error("Ticker not available");
		}
	}
	return iter->second;
}

const PairList& Interface::getPairs() const {
	auto now = std::chrono::system_clock::now();
	if (now < pairListExpire) return pairList;
	auto r = publicGET("/v2/conf/pub:info:pair");
	pairList =readPairs(r);
	pairListExpire = now+std::chrono::hours(1);
	return pairList;
}

bool Interface::isMarginPair(const StrViewA &name) {
	return name.endsWith(" (m)");
}


json::StrViewA Interface::stripMargin(const StrViewA &name) {
	if (isMarginPair(name)) return name.substr(0,name.length-4); else return name;
}

const bool Interface::hasKey() const {
	return !keyId.empty() && !keySecret.empty();
}

json::Value Interface::signRequest(const StrViewA path, json::Value body) const {

	auto nonce = (this->nonce)++;
	std::ostringstream buff;
	buff << "/api" << path << nonce;
	if (body.defined()) body.toStream(buff);
	std::string msg = buff.str();
	unsigned char digest[256];
	unsigned int digest_len = sizeof(digest);
	HMAC(EVP_sha384(),keySecret.data(), keySecret.length(), reinterpret_cast<const unsigned char *>(msg.data()), msg.length(), digest, &digest_len);
	json::String hexDigest(digest_len*2,[&](char *c){
		const char *hexletters = "0123456789abcdef";
		char *d = c;
		for (unsigned int i = 0; i < digest_len; i++) {
			*d++ = hexletters[digest[i] >> 4];
			*d++ = hexletters[digest[i] & 0xf];
		}
		return d-c;
	});
	return Object
			("bfx-nonce", nonce)
			("bfx-apikey", keyId)
			("bfx-signature", hexDigest);

}

template<typename Map>
std::string prepareUpdateRequest(const Map &map)  {
	std::ostringstream out;
	auto iter = map.begin();
	auto e = map.end();
	if (iter != e) {
		out << iter->first;
		++iter;
		while (iter != e) {
			out << "," << iter->first;
			++iter;
		}
	}
	return out.str();

}


void Interface::updateTickers() {
	std::string req = prepareUpdateRequest(tickers);
	if (req.empty()) return;
	req = "/v2/tickers?symbols="+req;
	Value v = publicGET(req);
	for (Value t: v) updateTicker(t);
	needUpdateTickers = false;
}

void Interface::updateTicker(Value v) {
	std::string symbol = v[0].getString();
	Ticker tk;
	tk.bid = v[1].getNumber();
	tk.ask = v[3].getNumber();
	tk.last = v[7].getNumber();
	tk.time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	tickers[std::move(symbol)] = std::move(tk);
}


double Interface::getFeeFromTrade(Value trade, const PairInfo &pair) {
	double price = trade[5].getNumber();
	double size = trade[4].getNumber();
	StrViewA feecur = trade[10].getString();
	double fee = trade[9].getNumber();
	double res;
	if (feecur == pair.asset) {
		res =  std::abs(fee/size);
	} else if (feecur == pair.currency) {
		res = std::abs(fee/(size * price));
	} else {
		const auto &p = getPairs();
		auto conv = findConversion(p, feecur, pair.currency);
		if (conv.empty()) {
			conv = findConversion(p, pair.currency, feecur);
			if (conv.empty()) return 0.002;
			auto tk = getTicker(conv.str());
			fee = fee / tk.last;
			res = std::abs(fee/(size * price));
		} else {
			auto tk = getTicker(conv.str());
			fee = fee * tk.last;
			res =  std::abs(fee/(size * price));
		}
	}
	res = std::min(0.002, res); //limit fee max to 0.2% (highest Bitfinex fee)
	return res;
}

Value Interface::signedPOST(StrViewA path, Value body) const {
	try {
		return api.POST(path, body,signRequest(path, body));
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v.join(" ").str());
		} catch (...) {
			throw;
		}
	}
}

Value Interface::publicGET(StrViewA path) const {
	try {
		return api_pub.GET(path);
	} catch (HTTPJson::UnknownStatusException &e) {
		try {
			json::Value v = json::Value::parse(e.response.getBody());
			throw std::runtime_error(v.join(" ").str());
		} catch (...) {
			throw;
		}
	}
}

json::Value Interface::getMarkets() const {
	auto pairs = getPairs();
	Object res;

	for (auto &&p: pairs) {
		auto sub = res.object(p.second.asset);
		if (p.second.leverage) {
			String symbol {p.second.symbol, p.second.leverage?" (m)":""};
			sub.set(p.second.currency, Object
					("Exchange",p.second.symbol)
					("Margin", symbol));
		} else {
			sub.set(p.second.currency, Object("Exchange",p.second.symbol));
		}
	}
	return res;
}

json::Value Interface::getWallet_direct() {
	auto data = signedPOST("/v2/auth/r/wallets", json::object);
	std::map<std::string_view, Object> mp;
	for (Value x: data) {
		Object &q = mp[x[0].getString()];
		double n = x[2].getNumber();
		if (n) {
			q.set(x[1].getString(), n);
		}
	}
	Object poss;

	if (!positions.has_value()) {
		auto data = signedPOST("/v2/auth/r/positions", json::object);
		positions = readPositions(data);
	}
	for (const auto &c: *positions) {
		poss.set(c.first, c.second);
	}



	Object out;
	for (auto &&x: mp) {
		out.set(x.first, x.second);
	}
	out.set("positions", poss);
	return out;
}
