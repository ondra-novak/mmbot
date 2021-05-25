/*
 * interface.cpp
 *
 *  Created on: 22. 5. 2021
 *      Author: ondra
 */
#include <sstream>

#include "interface.h"

#include <openssl/hmac.h>
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>
#include <imtjson/array.h>
#include "../isotime.h"
using json::Array;
using json::Object;
using json::Value;
using json::String;

Interface::Interface(const std::string &config_path)
:AbstractBrokerAPI(config_path,{
		Object
			("name","key")
			("label","Key")
			("type","string"),
		Object
			("name","secret")
			("label","Secret")
			("type","string")
})
,api(simpleServer::HttpClient(
		"Mozilla/5.0 (compatible; MMBOT/2.0; +http://github.com/ondra-novak/mmbot.git)",
		simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(60)),
		"https://www.southxchange.com/api")
,orderDB(std::string(config_path)+".orders", 1000)
{
	nonce = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())*1000;
}

json::Value Interface::testCall(const std::string_view &method, json::Value args) {
	if (method == "transactions") {
		return  apiPOST("/v3/listTransactions", Object
				("currency",args)
				("pageIndex",0)
				("pageSize",50)
				("sortField","Date")
				("descending",true)
		);
	} else {
		throw std::runtime_error("Not implemented");
	}
}

static std::pair<std::string_view, std::string_view> splitPair(const std::string_view &pair) {
	auto s = pair.find('-');
	if (s == pair.npos) return std::pair(pair,pair);
	else return std::pair(pair.substr(0,s),pair.substr(s+1));
}



IStockApi::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	updateMarkets();
	auto splt = splitPair(pair);
	MarketInfo minfo;
	minfo.asset_symbol = splt.first;
	minfo.currency_symbol = splt.second;
	auto iter = std::lower_bound(markets.begin(), markets.end(), minfo, marketMapCmp);
	if (iter == markets.end() || iter->asset_symbol != minfo.asset_symbol || iter->currency_symbol != minfo.currency_symbol) {
		throw std::runtime_error("Unknown symbol");
	}
	return *iter;
}

AbstractBrokerAPI* Interface::createSubaccount(
		const std::string &secure_storage_path) {
	return new Interface(secure_storage_path);
}

void Interface::onLoadApiKey(json::Value keyData) {
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();
}

json::Value Interface::apiPOST(const std::string_view &uri, json::Value params) {
	if (api_key.empty() || api_secret.empty()) throw std::runtime_error("Missing API key");
	json::Value req = params.merge(json::Value(json::object,{
		Value("nonce", nonce++),
		Value("key", api_key)
	}));

	static const char *hexchar="0123456789abcdef";

	unsigned char digest[100];
	unsigned int digest_len = sizeof(digest);

	String ss = req.toString();
	HMAC(EVP_sha512(), api_secret.data(), api_secret.length(),
			reinterpret_cast<const unsigned char *>(ss.str().data),
			ss.str().length, digest, &digest_len);
	char buff[200];
	for (unsigned int i = 0; i < digest_len; i++) {
		buff[i*2] = hexchar[digest[i]>>4];
		buff[i*2+1] = hexchar[digest[i] & 0xF];
	}
	Value hdr = Value(json::object,{Value("Hash", StrViewA(buff, digest_len*2))});

	return api.POST(uri, req, std::move(hdr));
}


IStockApi::BrokerInfo Interface::getBrokerInfo() {
	return {
		!api_key.empty() && !api_secret.empty(),
		"SouthExchange",
		"SouthExchange",
		"https://www.southxchange.com/",
		"1.0",
		"MIT",
		"iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAF10lEQVR42u1bX2xTZRQ/5+vtthYl"
		"GEVGAiGo4YEYXAyyrdDtlgfNXlRMWJBkoqKQoMlCnBsbf7zyZ1vrDFlUouAfcA+akUBMJMTE0G4F"
		"1iKaSQwPBF0IJAzRSKa0bL33Oz4MRsC1a+/5brdGzlMf+p3vnN/5/c53vntbgHv2/za89SEQig8j"
		"YhHHGRGdHjrf7/tx7/qU6kCJiJQljTiWt7i9gdymwPHi6Y8+sbWQGDAGQOT0xQ4giiqAt1kPxioK"
		"ofp3AAAHay1zGNaQJYeYG2gI0KUb4fumWvIZGQAA0Lu1fIAE1bNRFuIx9HreV9mjnKh+2g30UPyQ"
		"QFzBrRyB9Wyk0fftVKT+uAwYs0RiHUl5mbshkutTf2vvzKlI/YwARIzAH5JoLTcIFDhL07R9NpZq"
		"TlM/MwMAoGdT5VEA+Ji/ufacHoyvzbH6qXwknxEAAIA/f7/2tiXlOX4QtHtZW/SRSdC925YEbtmZ"
		"jmeuS5B1RGTyAhH3u13uL2FltyvPujdZAAAARJt8p4hgJ78aYmngqXlNeZvxJ6B+1gAAAEAysYtI"
		"nlIQ1zv+UN+Tk3Xk2QYgYgTMlJmqI5LXmYEVaVJ0VW486Zlyd4GJ7HiL/xwRNrCr48KFJaWinVM1"
		"VdW3M2qiHowdEULUsKdESz4daa78XqUU7IAoco0dMPkqEV1lT4ku8cWytugDBSOBsX7QGBiUlrVe"
		"AVXnFAn3HlVSsLtO2FnU0+w7LEnu58MvVgXa46vZOmb0D2F3YTIB9UQ0wAeBPqra2TN3nKSKs0y+"
		"OK8SuGVxo2JIAqwhkhZzQJrhKirZD4ZxdywjWboYmRQAAAB6GsujSNihoB8sD3hr6nOltoqjU3Ad"
		"DCYvbCOifn4/plZ914nHx0nSlSZ5AQqM7eSsUTtCKbOOSN5gSqEE3VrXQqP77kfzMu2RPBUAAACI"
		"bF76CwC2KJBCWaln3vaJqK5qalQGAABAOHG0k4iOsYWA1FAdivvTgaAyeTujcEbTg7E5iHAGUbAm"
		"PCIaSCSoLG5UDIHDJlQ6izRVXAKJbyqQwnyPFzrz8tzACafLg7GvQIhVXD+Wab7Q0+w7XDAMGJtM"
		"ZGqDtMwr/ElZ7PXv6p1dcAAUkmlOOC0S7j0gxCyuHynluujmqssFBUCgPb4aBLL1L0nud1r/yiWg"
		"B2NzQNCH7FmAaCCZgPp8SEAdAIYhEPEAfwaQlgRYM94M4MT7QmUABLw19Yi4nH0uE3b0NJZH0yWv"
		"GgQlAIze4qhVAfX7B5MXtk1UeZUgsJvgQqO7CN1aFyKWMKl/g1JW3VmjdiTLIqGKGyGbAaWeedsR"
		"sUzBUNoyeqv8T/WtNGyRky6B6lDcT0gNCqh/LJw42plr01MhBdt3gXIjNt3rxX5EnM+k/jVrZHhR"
		"75bqi3fPU0Q0nMXFqRgYzwVtM8DjhU5u8qMTD74xTvKQTfK5fE8pA6rbTq5wadohfvLy62NNFS+q"
		"oLbdByU5nwJ6KFyK4PpEge4vpWRqgypdExHl490gAnk+R8SZzOSJLPnK8Wb/X5N9G8wJAD0YX899"
		"M3wTgQ9Uvxm2uz5rAJa1RhcgEvslCFl09sag3OTUnJ+rn6x6gG6ENdTcXYhiGjO4EVPIur7dvuRU"
		"eSCSHQM83s2IYomC/d6NNlb+pLKLc1kwIQD+4MkliLCFH5Q8Ef7hQjBflc0WhIwALGr4bpoA0YWI"
		"GjP5v1NW6iU4WGs5cZZzJJ4RgAcfnvGeS4gF/GrgxuPN/t+cHGjSsGDCn9ym3ay6va9GCHGEGxCR"
		"+U240fd8rpXL1++FRZqu/5BA/IydvKQrpmm+bmOpma9+ML4EvN69KMRs7qaE1mvRlqqrqqvm6DGo"
		"h/pe5v5b5CYE+zj/FrkJgnCaBXdsULUjPh8lsl9KkpTnKZF8S0XcTkvhNgAru11aMRxAl5jO3MQk"
		"gLqIEfjH6QamVAL64rkNgOhXAHNbpKkipjJIJ6dEcXsTsV2B89NDv/68A+7ZPSsY+xdlD5aypNri"
		"LgAAAABJRU5ErkJggg==",
		false,
		true
	};
}

json::Value Interface::getMarkets() const {
	const_cast<Interface *>(this)->updateMarkets();
	Object lst;
	Object sub;
	std::string_view curSymb;
	for (const MarketInfo &mm: markets) {
		if (curSymb != mm.asset_symbol) {
			if (!curSymb.empty()) {
				lst.set(curSymb, sub);
				sub.clear();
			}
			curSymb = mm.asset_symbol;
		}
		sub.set(mm.currency_symbol, String({mm.asset_symbol,"-", mm.currency_symbol}));
	}
	lst.set(curSymb, sub);
	return lst;

}

double Interface::getBalance(const std::string_view &symb, const std::string_view &pair) {
	if (!cacheBalance.defined()) {
		cacheBalance = apiPOST("/v3/listBalances", json::object);
	}
	Value res = cacheBalance.find([&](const json::Value &c){
		return c["Currency"].getString() == symb;
	});
	return res["Deposited"].getNumber();


}

IStockApi::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	auto splt = splitPair(pair);
	auto &a_tx = txCache[std::string(splt.first)];
	bool needFees = false;
	if (!a_tx.defined()) a_tx = apiPOST("/v3/listTransactions", Object
			("currency",splt.first)
			("pageIndex",0)
			("pageSize",50)
			("sortField","Date")
			("descending",true));
	std::vector<Trade> trades;
	Array newLastId;
	for (Value z: a_tx["Result"]) {
		if (z["Type"].getString() == "trade" && z["OtherCurrency"].getString() == StrViewA(splt.second)) {
			Value id = z["TradeId"];
			newLastId.add(id);
			if (lastId.type() == json::array && lastId.indexOf(id) == Value::npos) {
				double a = z["Amount"].getNumber();
				double p = z["Price"].getNumber();
				trades.push_back({
					z["TradeId"],
					parseTime(z["Date"].toString(), ParseTimeFormat::iso_notm),
					a,p,a,p
				});
				needFees = true;
			}
		}
	}

	for (Value z: a_tx["Result"]) {
		if (z["Type"].getString() == "tradefee") {
			Value id = z["TradeId"];
			auto iter = std::find_if(trades.begin(), trades.end(),[&](const Trade &tx){
				return tx.id == id;
			});
			if (iter != trades.end()) {
				double amount = z["Amount"].getNumber();
				iter->eff_size+=amount;
			}
		}
	}
	if (needFees) {
		auto &c_tx = txCache[std::string(splt.second)];
		if (!c_tx.defined()) c_tx = apiPOST("/v3/listTransactions", Object
				("currency",splt.second)
				("pageIndex",0)
				("pageSize",50)
				("sortField","Date")
				("descending",true));

		for (Value z: c_tx["Result"]) {
			if (z["Type"].getString() == "tradefee") {
				Value id = z["TradeId"];
				auto iter = std::find_if(trades.begin(), trades.end(),[&](const Trade &tx){
					return tx.id == id;
				});
				if (iter != trades.end()) {
					if (iter->eff_size) {
						double amount = z["Amount"].getNumber();
						double total = -iter->price*iter->eff_size+amount;
						iter->eff_price = -total/iter->eff_size;
					}
				}
			}
		}
	}
	return TradesSync {
		trades, newLastId
	};
}

bool Interface::reset() {
	updateMarkets();
	cacheBalance = Value();
	cacheOrders = Value();
	txCache.clear();
	return true;
}

IStockApi::Orders Interface::getOpenOrders(const std::string_view &pair) {
	if (!cacheOrders.defined()) {
		cacheOrders = apiPOST("/v3//listOrders",json::object);
	}
	auto splt = splitPair(pair);
	Value myOrders = cacheOrders.filter([&](Value x){
		return x["ListingCurrency"].getString() == splt.first
				&&  x["ReferenceCurrency"].getString() == splt.second;
	});
	return mapJSON(myOrders, [&](Value ord){
		auto code = ord["Code"];
		return Order{
			code,
			orderDB.get(code),
			(ord["Type"].getString() == "sell"?-1:1)*ord["Amount"].getNumber(),
			ord["LimitPrice"].getNumber()
		};
	}, Orders());


}

json::Value Interface::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {

	if (replaceId.defined()) {
		apiPOST("/v3/cancelOrder", Value(json::object,{Value("orderCode", replaceId)}));
	}
	if (size) {
		auto splt = splitPair(pair);
		Value resp = apiPOST("/v3/placeOrder", Object
				("listingCurrency",splt.first)
				("referenceCurrency",splt.second)
				("type",size>0?"buy":"sell")
				("amount",std::abs(size))
				("amountInReferenceCurrency",false)
				("limitPrice",price));
		orderDB.store(resp, clientId);
		return resp;
	} else {
		return nullptr;
	}
}

double Interface::getFees(const std::string_view &pair) {
	return 0.001;
}


IBrokerControl::AllWallets Interface::getWallet() {
	if (!cacheBalance.defined()) {
		cacheBalance = apiPOST("/v3/listBalances", json::object);
	}
	return AllWallets{Wallet{"spot",mapJSON(cacheBalance, [](Value v){
		return WalletItem{v["Currency"].toString(), v["Deposited"].getNumber()};
	}, std::vector<WalletItem>())}};


}

IStockApi::Ticker Interface::getTicker(const std::string_view &pair) {
	auto splt = splitPair(pair);
	std::ostringstream uri;
	uri << "/v3/price/" << splt.first << "/" << splt.second;
	Value v = api.GET(uri.str());
	return {
		v["Bid"].getNumber(),
		v["Ask"].getNumber(),
		v["Last"].getNumber(),
		static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count())
	};
}

std::vector<std::string> Interface::getAllPairs() {
	updateMarkets();
	std::vector<std::string> res;
	std::transform(markets.begin(), markets.end(), std::back_inserter(res),[](const MarketInfo &nfo){
		return nfo.asset_symbol+"-"+nfo.currency_symbol;
	});
	return res;
}
void Interface::onInit() {

}
double Interface::getBalance(const std::string_view & symb)  {
	return 0;
}

void Interface::updateMarkets() {
	auto now = std::chrono::system_clock::now();
	if (now>marketMapExp) {
		try {
			MarketMap mmp;
			Value m = api.GET("/v3/fees");
			std::vector<std::pair<std::string_view,int> > currencies;
			for (Value cur:m["Currencies"]) {
				currencies.push_back(std::pair<std::string_view,int>(
						cur["Code"].getString(), cur["Precision"].getInt()));
			}

			std::sort(currencies.begin(), currencies.end());

			auto findCur = [&](const std::string_view &name){
				auto iter = std::lower_bound(
						currencies.begin(), currencies.end(), std::pair(name, 0));
				if (iter == currencies.end() || iter->first != name) return -1;
				else return iter->second;
			};


			for (Value mrk: m["Markets"]) {
				auto an = mrk["ListingCurrencyCode"];
				auto cn = mrk["ReferenceCurrencyCode"];
				int ap = findCur(an.getString());
				int cp = findCur(cn.getString());
				if (ap < 0 || cp < 0) continue;
				int min_ap = ap-3;
				int min_vp = cp-3;

				MarketInfo nfo;
				nfo.asset_step = std::pow(10,-ap);
				nfo.asset_symbol = an.getString();
				nfo.currency_step = std::pow(10,-cp);
				nfo.currency_symbol = cn.getString();
				nfo.feeScheme = FeeScheme::income;
				nfo.fees = 0.001;
				nfo.invert_price = false;
				nfo.leverage=0;
				nfo.min_size = std::pow(10,-min_ap);
				nfo.min_volume = std::pow(10,-min_vp);
				nfo.private_chart = false;
				nfo.simulator = false;				;

				mmp.push_back(nfo);
			}

			std::sort(mmp.begin(), mmp.end(), marketMapCmp);

			std::swap(markets, mmp);

			marketMapExp = now + std::chrono::hours(1);
		} catch (std::exception &e) {
			if (markets.empty()) throw;
		}
	}

}

bool Interface::marketMapCmp(const MarketInfo &a,const MarketInfo &b) {
	std::less<std::string_view> ls;
	if (ls(a.asset_symbol, b.asset_symbol)) return true;
	if (ls(b.asset_symbol, a.asset_symbol)) return false;
	return ls(a.currency_symbol, b.currency_symbol);
}
