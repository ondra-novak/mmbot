/*
 * bybitbroker.cpp
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */

#include "bybitbroker.h"

#include <openssl/hmac.h>
#include <cmath>
#include <ctime>

#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <imtjson/operations.h>
#include <imtjson/binjson.tcc>


const std::string licence(R"mit(Copyright (c) 2020 Ondřej Novák

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
OTHER DEALINGS IN THE SOFTWARE.)mit");

const std::string icon(
		"iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAKv0lEQVR42u1aa4xV1RX+9j7n3Duv"
		"Oy8EHRk0JFgjPmaKUv0HwYp/FNQWGpPWR9AfavvPFE0rhIcU7EOh/qkKNUZIrRk7NsaArSG2qdqk"
		"yTwQFe9URxCEznAH5nHv3HvPOas/Zp0za/acc+cOmNaWu5Od89hnv7691tprffsAlVRJlVRJlQR1"
		"IU9c85U4B4kuBAC0cb3gkr7Qdd/+P7JXdL4SoP4HJq7Oo3zWEqC+RoZQlbFI9FWogDnp/zYIyljd"
		"uFUmI3+lNkCVQFmVuQLqPEHQnC3OEggC4APw+OrOBgj1NdZ/JSZvA3AAJPlqCfsVTLrAuWiAQDFS"
		"TlICqIQxof+wuJMYg8VjrAZQC6AOQA0DYfG3HoBxAGMARgFk+dllcM5JBZRAarbSQech+srYoRye"
		"fAOAOQAuAtDIYDjcV4EnPgRgEMBpbicOBJJXO0LXAvGiMixtnKTQOaiXMu4tAFUA6gHMBbAAwHwA"
		"8/hdgr8dB3AWwEkGS27tBQbABKFoSoDFDSaEkaEIKysnGmWRffGtKsN6l1r9QPSbAbQAuBzAQr5v"
		"YDUgBmAIQIrrBBPWMVIg78nmSTdwA07EQElYWgmENibmi0wCWB3xbTnGz2Z9vwjApSwBlwG4xJCA"
		"vBi7x6tODFDWAICE2uQBjNsArmT9quIBmwaJIrYaqS7KKHfFCgRGTFrtckEIVKAZQCuv/FwAc97d"
		"lqpbdPGIA4AOdDvFH/y6GNiDYAdw2FbkxHikRAfjzNsAbmKjkuDBmisVVHYNAOSeHDRa5OwLI+Zw"
		"u3qWRlFx3RTr/cUAmgDUpap1dap6Qn1rkiqQWin69QCGeZW9CAnweZwFG8C1vL1UxQxUrq4JgBYG"
		"M0BVAmCfIwBSzZKsos08sSrhC0TFM0mW6JwYi2nDgsXK2QCuYJSrhA1QMTbAEwZOG2SKVAEy9nHT"
		"e5spoiOjjSq2BzURjpBi6ZXfNonJ+xFzCQDI2qxbKUbONhqmOF9ba61835fok1LKJyI/xk4oAEop"
		"BaUUfN9HGSBAGETHmLwE0RL9OUJSKWY38wEULcvKagD1mzdvqSeiFOc6M+fz+VRv76HU+vXr623b"
		"rgdQ7/t+/XvvvV8X1MtkhlJNTXPqWVwbOTccPvxh2G5/f38dEdX5vl+Xy+XqovoKvj127FjqlVd+"
		"n2L1rDYWSEX5Lz09PQ4RVRFRNRHVco7sJ5cbr/M8L6UBJF23GPgAjmG4HABOIpFwrr32msT27dsT"
		"W7duTSqlkgASzz77bIKIHABOU1Nj4vHHHwvKqgAk1617MLl48VVBu4ndu/eE7RcKRcfob0pubW11"
		"1q5d43R1dcetvOlfKM/z5M5jG3OZ8lwsFmwAtjaMmfZ9X/f19el0Oq3T6bT+/POjmmiy/KGHHtZE"
		"pAHovXtf1gcPvhOW3X333ZqIgt3BevDBdWHZ4cMf6i1bNmvZV5CPHz8R9pdO9+nx8fGwrL29TW/c"
		"uDFq4tOcqKNHj6p0Oq2CtjKZTBhQjY6OaS5DOp3Gp59+FgRROLVhw4ZRIioQkZvL5fzrrmuXTo2/"
		"ZctWn4jC/NhjPwnLVq++w3ddV5Q97gPwly69yS8WJ99v2LhxSptnzw6HZTt2/GJKWXv7Df7g4GBY"
		"/v77f5cOFh16uoFy+0C5faCORxOmrxLmt956K2yjq6snMHzjvEV+CeCILsc6P/HETzEwMBg+t7Ze"
		"Gt6//non3nnnL+HzvffeCwD45a9+Dtue2KkOHfoAmzdtKtsB6O7+B7q6usPnVKpuNl5kKeMa7P9Z"
		"jh8GyuEE1YoVN6vm5uZQ57q6e6f4+E899ZQqFl0FQF155TfUI4/8SF2/ZElYvnffPhUTG8TmBQsW"
		"hPcDg4PnEolGufOukIB/AfhiGiVm2zZ27nxaeZ4H3u7wrRtvhGVNYHXkyCfY/cJvplQ6cGA/Dh48"
		"iJUrb4FSCluf3IKamhoAQE9PL3Zs/1nJUd966y24/vo/h8+LFl2Byy+/DADgui6eeWbX+fKAJIiT"
		"HAdPXwLon8YH2LaN5cuXR7Zy7NgxrFm7JrJs06ZNWLZsGZLJBBobGsL3e/bsmXF0bW3XRb7PFwrY"
		"sX07Ov/Qcd7kp0GeDAMYAPDFrA5G5s2bh0ce/mFk2bvv/g1vv/32lHddXd3YtWvnOVNECcfBbbfd"
		"DsdJlCviURRYlAucZRAGp0lAoVDE88+/gKI7wRlY2sLNN6/A4sVXIZlM4oEH1mH//gPo7OyYxvxs"
		"27YNK1feAtueaPbVV18ta6JvvPEmPuvvB0AgnzB//qW48847oLXGkiXfREdHB61adXucpxhH1KgY"
		"gORuMG4CQL7vqeeee456e7unGMYTJ06gpaUFlqXp/vvvQWfndLF0XRee5yuePzKZM2XRZB9+9BHW"
		"//jRSb/WstDR8RpWr15FANDe3ha1yhQRrqsIP0P2KrdKF4Bvz6Bb4buhoTNoaWkBANTU1Jaix2jW"
		"ltp443kehoYy4XNtbR3V1zf4w8NnfQDe7/6a9eY3TwRf3Z+5vhEK2yK0n4xpaBpYFABghorTVuiu"
		"u74bWmUANDY2RiXETs2eKJ2OyQ03LA1XPZfL0sjIcBBq5598rViY5B58V0SpAYtUO+GOKxVKgYre"
		"GWzTeDiOQy+99CKIJgd19dXXwHHscECdr//RN0JNbURkiOETI13ZNWu+g5Urvz3xMRFaWxdg7tyL"
		"wkH29f3TJaI8G68Rg/4uMgCag6YmrjdVBWhy1UX2bOOFsixLtbW1xe6lb76533/xt7uLgiES4aqy"
		"DSmiiJhcm0AtXLgwtr/TpzPu8uXL8jzpDFPfg+zJZUXcn+TJu0INLAG7JHXCbAPI27YjaaVIC3ry"
		"5Ek6cOBP7n333VNkZyLP5KLm6K9aKZW0bSsIWcmyrDgmyXYcR8ewRASARkZH6cjHH7trv/f9gPc/"
		"zc7LF3w9LQDQHDZfwu3XAKh2HNsO+X/LLvKYg1wEUFAAemIIEZPu9rhiTohhgTusZbqqltsJ2gj0"
		"tsD3GoCjlEpyGG3F2AxfbFdjAM4w738UQD+A4ywNWf7OYR6ilRmuRXzfyIujeAzDXPcTAB8A6LUZ"
		"zVHWH5NwMEnEHDdyhkUwz3UaWfwauB1L0M/jgp/TAKqIqEYAbkWc2ZHYq0dY5I8zAEcBnOIx5Plb"
		"h78NFqNGnAtIAIIDlAF2h0dtAH2CFHUiKDESh49Z7niIGysICnoOA1AjDigCw5UV6hJY6WpBdKgZ"
		"orZBXqgveQJDLBnBCU+C69hCAnO8KKYEnGI1OgUgYwPoLsEKS++pIFzIs2IAAXXdyNdAAnyukzN0"
		"tZpBqOKBa+MsUgIQ+O1DrPMZYfzywri6wvIHR2IZVssktx2oU4YnfxzAGcWnLReLj6NWxBOhZE5s"
		"QT5/Xy1WNSGo8qI4hXEFbZ0UzkopGj4APTj5HeN+C2LvlwepVbwI9YJLdMS5hdxNBgBklWBVJQFp"
		"x1DJplEjw/tKRByWuOJ0JtgFTI5PxURunjj3L4h+za1VMsIJAbIj+pDh8ChffbNjiytWxQQTnnFE"
		"RsbfG+ZZAUWcF+oyzgvNunLvLvXjg/yLxDb68A1AKY4FUoYxjAs5KcYNViXqqTLc5lL9zfTrizkO"
		"HSFVU84J1QwNzYaJmYmzO5efLeL6ozL/MSjlkldSJVVSJVVSJVVSJVVSJVVSJVXSBZv+DQFQGg/g"
		"2aoaAAAAAElFTkSuQmCC"
);

using namespace json;

 Value APIKeyFormat({
		Object{
			{"label","API Key"},
			{"name","key"},
			{"type","string"}
		},
		Object{
			{"label","API Secret"},
			{"name","secret"},
			{"type","string"}
		},
		Object{
			{"label","Server"},
			{"name","server"},
			{"type","enum"},
			{"options",Object{
				{"live","Live"},
				{"testnet","Testnet (paper trading)"},
			}}
		}
});


ByBitBroker::ByBitBroker(const std::string &secure_storage_path)
	:AbstractBrokerAPI(secure_storage_path, APIKeyFormat)
 	 ,curTime(15)
 	 ,api(simpleServer::HttpClient("MMBOT (+https://www.mmbot.trade)", simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(15)),"https://api.bybit.com")
{
	nonce = std::time(nullptr);
}

json::Value ByBitBroker::testCall(const std::string_view &method, json::Value args) {
	return nullptr;
}

std::vector<std::string> ByBitBroker::getAllPairs() {
	updateSymbols();
	std::vector<std::string> ret;
	ret.reserve(symbols.size());
	for (const auto &k: symbols) {
		ret.push_back(k.first);
	}
	return ret;
}

IStockApi::MarketInfo ByBitBroker::getMarketInfo(const std::string_view &pair) {
	MarketInfoEx nfo = getSymbol(pair);
	Value pos;
	switch (nfo.type) {
	case spot:break;
	case inverse_futures:
		pos = getInverseFuturePosition(nfo.name);
		if (pos.defined() && pos["leverage"].defined()) {
			nfo.leverage = pos["leverage"].getNumber();
		}
		break;
	case inverse_perpetual:
		pos = getInversePerpetualPosition(nfo.name);
		if (pos.defined() && pos["leverage"].defined()) {
			nfo.leverage = pos["leverage"].getNumber();
		}
		break;
	case usdt_perpetual:
		pos = getUSDTPerpetualPosition(nfo.name);
		if (pos.defined() && pos["leverage"].defined()) {
			nfo.leverage = pos["leverage"].getNumber();
		}
		break;
	}
	return nfo;
}

AbstractBrokerAPI* ByBitBroker::createSubaccount(const std::string &secure_storage_path) {
	return new ByBitBroker(secure_storage_path);
}

IStockApi::BrokerInfo ByBitBroker::getBrokerInfo() {
	return IStockApi::BrokerInfo{
		hasKeys(),
		"bybit",
		"ByBit",
		"https://www.bybit.com/en-US/invite?ref=Y6ERW0",
		"1.0",
		licence,
		icon,
		false,
		true,
	};
}

void ByBitBroker::onLoadApiKey(json::Value keyData) {
	api_key = keyData["key"].getString();
	api_secret = keyData["secret"].getString();
	testnet = keyData["server"].getString() == "testnet";
	if (testnet) {
		api.setBaseUrl("https://api-testnet.bybit.com");
	} else {
		api.setBaseUrl("https://api.bybit.com");
	}
}

static std::string_view extractExpiration(std::string_view str) {
	auto l = str.length();
	while (l>0) {
		if (!isdigit(str[l-1])) return str.substr(l);
		l--;
	}
	return std::string_view();
}

json::Value ByBitBroker::getMarkets() const {
	Object inversed;
	Object usdt;
	Object mspot;
	ondra_shared::linear_map<std::string_view, Object> exp_futures;
	updateSymbols();
	for(const auto &sdef: symbols) {
		switch (sdef.second.type) {
		case inverse_perpetual:
			inversed.set(sdef.second.currency_symbol+"/"+sdef.second.inverted_symbol, sdef.first);
			break;
		case inverse_futures:
			exp_futures[sdef.second.expiration].set(sdef.second.currency_symbol+"/"+sdef.second.inverted_symbol, sdef.first);
			break;
		case usdt_perpetual:
			usdt.set(sdef.second.asset_symbol+"/"+sdef.second.currency_symbol, sdef.first);
			break;
		case spot:
			mspot.set(sdef.second.asset_symbol,mspot[sdef.second.asset_symbol].replace(sdef.second.currency_symbol, sdef.first));
			break;
		}
	}
	return Object{
		{"Inverse Perpetual", inversed},
		{"USDT Perpetual", usdt},
		{"Spot", mspot},
		{"Inverse Futures", Value(json::object, exp_futures.begin(), exp_futures.end(),[](const auto &item){
			return Value(item.first, item.second);
		})
		}
	};
}

double ByBitBroker::getBalance(const std::string_view &symb, const std::string_view &pair) {
	const MarketInfoEx &market = getSymbol(pair);
	switch (market.type) {
	case inverse_futures: {
		if (symb == market.asset_symbol) {
			Value pos = getInverseFuturePosition(market.name);
			return pos["size"].getNumber() * (pos["side"].getString() == "Sell"?1.0:-1.0);
		} else {
			Value coin = getWalletState(symb);
			return coin["equity"].getNumber();
		}
	}break;
	case usdt_perpetual: {
		if (symb == market.asset_symbol) {
			Value pos = getUSDTPerpetualPosition(market.name);
			return pos["size"].getNumber() * (pos["side"].getString() == "Sell"?-1.0:1.0);
		} else {
			Value coin = getWalletState(symb);
			return coin["equity"].getNumber();
		}
	}break;
	case inverse_perpetual: {
		if (symb == market.asset_symbol) {
			Value pos = getInversePerpetualPosition(market.name);
			return pos["size"].getNumber() * (pos["side"].getString() == "Sell"?1.0:-1.0);
		} else {
			Value coin = getWalletState(symb);
			return coin["equity"].getNumber();
		}
	}break;
	case spot:
		return getSpotBalance(symb)["total"].getNumber();
	}
	return 0;
}


IStockApi::TradesSync ByBitBroker::syncTrades(json::Value lastId, const std::string_view &pair) {
	const MarketInfoEx &nfo = getSymbol(pair);
	Value list;
	Value fltlist;
	std::uint64_t since;
	switch (nfo.type) {
	case inverse_perpetual:
		if (lastId.hasValue()) {
			since = lastId["time"].getUIntLong();
			list = privateGET("/v2/private/execution/list", Object{{"symbol", nfo.name},{"start_time",since}})["trade_list"];
			fltlist = list.filter([l=lastId["seen"],&since](Value x)->bool{
				std::uint64_t time = x["trade_time_ms"].getUIntLong();
				since = std::max(since, time);
				return l.indexOf(x["exec_id"]) == l.npos && x["exec_type"].getString() != "Funding";
			});
			if (fltlist.empty() && !list.empty()) since++;
			return TradesSync{mapJSON(fltlist,[&](Value x){
				double side = x["side"].getString() == "Buy"?-1:1;
				double size = x["exec_qty"].getNumber()*side;
				double price = 1/x["exec_price"].getNumber();
				double fee = x["exec_fee"].getNumber();
				double eff_price = price;
				if (fee > 0) {
							eff_price += -fee/size;
						}
				return IStockApi::Trade{x["exec_id"],x["trade_time_ms"].getUIntLong(),size,price,size,eff_price};
			}, TradeHistory()),Object{{"time",since},{"seen",list.map([](Value x){
				return x["exec_id"];
			})}}};
		} else {
			return TradesSync{{},Object{{"time",curTime.getCurTime()},{"seen",json::array}}};
		}
	break;
	case usdt_perpetual:
		if (lastId.hasValue()) {
			since = lastId["time"].getUIntLong();
			list = privateGET("/private/linear/trade/execution/list", Object{{"symbol",nfo.name},{"start_time",since}})["data"];
			fltlist = list.filter([l=lastId["seen"],&since](Value x)->bool{
				std::uint64_t time = x["trade_time_ms"].getUIntLong();
				since = std::max(since, time);
				return l.indexOf(x["exec_id"]) == l.npos && x["exec_type"].getString() != "Funding";
			});
			if (fltlist.empty() && !list.empty()) since++;
			return TradesSync{mapJSON(fltlist,[&](Value x){
				double side = x["side"].getString() == "Buy"?1:-1;
				double size = x["exec_qty"].getNumber()*side;
				double price = x["exec_price"].getNumber();
				double fee = x["exec_fee"].getNumber();
				double eff_price = price;
				if (fee > 0) {
							eff_price += fee/size;
				}
				return IStockApi::Trade{x["exec_id"],x["trade_time_ms"].getUIntLong(),size,price,size,eff_price};
			}, TradeHistory()),Object{{"time",since},{"seen",list.map([](Value x){
				return x["exec_id"];
			})}}};

		} else {
			return TradesSync{{},Object{{"time",curTime.getCurTime()},{"seen",json::array}}};
		}
	break;
	case inverse_futures:
		if (lastId.hasValue()) {
					since = lastId["time"].getUIntLong();
					list = privateGET("/futures/private/execution/list", Object{{"symbol", nfo.name},{"start_time",since}})["trade_list"];
					fltlist = list.filter([l=lastId["seen"],&since](Value x)->bool{
						std::uint64_t time = x["trade_time_ms"].getUIntLong();
						since = std::max(since, time);
						return l.indexOf(x["exec_id"]) == l.npos && x["exec_type"].getString() != "Funding";
					});
					if (fltlist.empty() && !list.empty()) since++;
					return TradesSync{mapJSON(fltlist,[&](Value x){
						double side = x["side"].getString() == "Buy"?-1:1;
						double size = x["exec_qty"].getNumber()*side;
						double price = 1/x["exec_price"].getNumber();
						double fee = x["exec_fee"].getNumber();
						double eff_price = price;
						if (fee > 0) {
									eff_price += -fee/size;
								}
						return IStockApi::Trade{x["exec_id"],x["trade_time_ms"].getUIntLong(),size,price,size,eff_price};
					}, TradeHistory()),Object{{"time",since},{"seen",list.map([](Value x){
						return x["exec_id"];
					})}}};
				} else {
					return TradesSync{{},Object{{"time",curTime.getCurTime()},{"seen",json::array}}};
				}
		break;
	case spot:
		if (lastId.hasValue()) {
			list = privateGET("/spot/v1/myTrades", Object{{"symbol",nfo.name},{"fromId", lastId}});
			fltlist = list.filter([&](Value x){
				int cmp = Value::compare(lastId,x["id"]);
				if (cmp < 0) lastId = x["id"];
				return cmp != 0;
			});
			return TradesSync{mapJSON(fltlist,[&](Value x){
				double side = x["isBuyer"].getBool()?1:-1;
				double size = x["qty"].getNumber()*side;
				double price = x["price"].getNumber();
				double fee = x["commission"].getNumber();
				bool assetFee = x["commissionAsset"].getString() == nfo.asset_symbol;
				bool currencyFee = x["commissionAsset"].getString() == nfo.currency_symbol;
				double eff_price = price;
				double eff_size = size;
				if (assetFee) {
					eff_size -= fee;
				}
				if (currencyFee) {
					eff_price += fee/size;
				}
				return IStockApi::Trade{x["exec_id"],x["time"].getUIntLong(),size,price,size,eff_price};
			}, TradeHistory()),lastId};

		} else {
			list = privateGET("/spot/v1/myTrades", Object{{"symbol",nfo.name},{"limit", 1}});
			if (list.empty()) {
				return TradesSync{{},1};
			} else {
				return TradesSync{{},list[0]["id"]};
			}
		}

		break;
	}
	return {};
}

bool ByBitBroker::reset() {
	positionCache.clear();
	walletCache.clear();
	spotBalanceCache = json::undefined;
	return true;
}

static Value parseOrderID(Value v) {
	if (v.type() != json::string || v.getString().empty()) return json::Value();
	try {
		auto str = v.getBinary(base64url);
		std::size_t idx = 0;
		Value ret = Value::parseBinary([&](){
			if (idx >= str.size()) throw std::runtime_error("not valid client ID");
			return str[idx++];
		}, base64);
		return ret[0];
	} catch (...) {
		return json::Value();
	}
}

json::Value ByBitBroker::composeOrderID(json::Value userData) {
	std::uint64_t n = nonce++;
	Value msg = {userData.stripKey(), n};
	unsigned char buff[27];
	int len = 0;
	msg.serializeBinary([&](unsigned char c){
		if (len < 27) {
			buff[len++] = c;
		} else {
			throw std::runtime_error("UserData are too long");
		}
	});
	return Value(json::BinaryView(buff,len), base64url);
}

IStockApi::Orders ByBitBroker::getOpenOrders(const std::string_view &pair) {
	const MarketInfoEx &nfo = getSymbol(pair);
	Value list;
	switch (nfo.type) {
	case inverse_perpetual:
		list = privateGET("/v2/private/order", Object{{"symbol",nfo.name}});
		return mapJSON(list, [&](Value x){
			return IStockApi::Order {
				x["order_id"],
				parseOrderID(x["order_link_id"]),
				x["qty"].getNumber()*(x["side"].getString()=="Buy"?-1:1),
				1.0/x["price"].getNumber()
			};
		}, IStockApi::Orders());
	break;
	case usdt_perpetual:
		list = privateGET("/private/linear/order/search", Object{{"symbol",nfo.name}});
		return mapJSON(list, [&](Value x){
			return IStockApi::Order {
				x["order_id"],
				parseOrderID(x["order_link_id"]),
				x["qty"].getNumber()*(x["side"].getString()=="Buy"?1:-1),
				x["price"].getNumber()
			};
		}, IStockApi::Orders());
	break;
	case inverse_futures:
		list = privateGET("/futures/private/order", Object{{"symbol",nfo.name}});
		return mapJSON(list, [&](Value x){
			return IStockApi::Order {
				x["order_id"],
				parseOrderID(x["order_link_id"]),
				x["qty"].getNumber()*(x["side"].getString()=="Buy"?-1:1),
				1.0/x["price"].getNumber()
			};
		}, IStockApi::Orders());
	break;
	case spot:
		list = privateGET("/spot/v1/open-orders", Object{{"symbol",nfo.name}});
		return mapJSON(list, [&](Value x){
			return IStockApi::Order {
				x["exchangeId"],
				parseOrderID(x["orderLinkId"]),
				x["origQty"].getNumber()*(x["side"].getString()=="Buy"?1:-1),
				x["price"].getNumber()
			};
		}, IStockApi::Orders());
	break;
	}
	return {};
}

static Value adjustSize(double size, double step) {
	return Value(std::round(std::abs(size)/step)*step);
}
static Value adjustPrice(double size, double step) {
	return Value(std::round(std::abs(size)/step)*step);
}

json::Value ByBitBroker::placeOrder(const std::string_view &pair, double size, double price, json::Value clientId, json::Value replaceId, double replaceSize) {
	const MarketInfoEx &nfo = getSymbol(pair);
	Value list;
	switch (nfo.type) {
	case inverse_perpetual:
		if (replaceId.hasValue()) {
			Value r = privatePOST("/v2/private/order/cancel",Object{{"symbol",nfo.name},{"order_id",replaceId}});
			double lv = r["leaves_qty"].getNumber();
			if (lv < std::abs(replaceSize*0.99)) return nullptr;
		}
		if (size) {
			Value r = privatePOST("/v2/private/order/create",Object{
				{"symbol",nfo.name},
				{"side",size<0?"Buy":"Sell"},
				{"order_type","Limit"},
				{"qty",adjustSize(size,nfo.asset_step)},
				{"price",adjustPrice(1.0/price,nfo.currency_step)},
				{"time_in_force","PostOnly"},
				{"order_link_id",composeOrderID(clientId)}
			});
			return r["order_id"];
		}
		break;
	case usdt_perpetual:
		if (replaceId.hasValue()) {
			privatePOST("/private/linear/order/cancel",Object{{"symbol",nfo.name},{"order_id",replaceId}});
		}
		if (size) {
			Value r = privatePOST("/private/linear/order/create",Object{
				{"symbol",nfo.name},
				{"side",size>0?"Buy":"Sell"},
				{"order_type","Limit"},
				{"qty",adjustSize(size,nfo.asset_step)},
				{"price",adjustPrice(price,nfo.currency_step)},
				{"time_in_force","PostOnly"},
				{"order_link_id",composeOrderID(clientId)}
			});
			return r["order_id"];
		}
		break;
	case inverse_futures:
		if (replaceId.hasValue()) {
			Value r = privatePOST("/futures/private/order/cancel",Object{{"symbol",nfo.name},{"order_id",replaceId}});
			double lv = r["leaves_qty"].getNumber();
			if (lv < std::abs(replaceSize*0.99)) return nullptr;
		}
		if (size) {
			Value r = privatePOST("/futures/private/order/create",Object{
				{"symbol",nfo.name},
				{"side",size<0?"Buy":"Sell"},
				{"order_type","Limit"},
				{"qty",adjustSize(size,nfo.asset_step)},
				{"price",adjustPrice(1.0/price,nfo.currency_step)},
				{"time_in_force","PostOnly"},
				{"order_link_id",composeOrderID(clientId)}
			});
			return r["order_id"];
		}
		break;
	case spot:
		if (replaceId.hasValue()) {
			Value r = privateDELETESpot("/spot/v1/order",Object{{"orderId",replaceId}});
			double lv = r["origQty"].getNumber()-r["executedQty"].getNumber();
			if (lv < std::abs(replaceSize*0.99)) return nullptr;
		}
		if (size) {
			Value r = privatePOSTSpot("/spot/v1/order",Object{
				{"symbol",nfo.name},
				{"side",size>0?"Buy":"Sell"},
				{"type","LIMIT_MAKER"},
				{"qty",adjustSize(size,nfo.asset_step)},
				{"price",adjustPrice(price,nfo.currency_step)},
				{"time_in_force","GTC"},
				{"orderLinkId",composeOrderID(clientId)}
			});
			return r["orderId"];
		}
		break;

	}
	return nullptr;
}

double ByBitBroker::getFees(const std::string_view &pair) {
	return 0;
}

IStockApi::Ticker ByBitBroker::getTicker(const std::string_view &pair) {
	const MarketInfoEx &nfo = getSymbol(pair);
	if (nfo.type == spot) {
		Value resp = publicGET("/spot/quote/v1/ticker/book_ticker?symbol="+nfo.name);
		auto bid = resp["bidPrice"].getNumber();
		auto ask = resp["askPrice"].getNumber();
		return {
			bid,
			ask,
			std::sqrt(bid*ask),
			curTime.getCurTime()
		};
	} else {
		Value resp = publicGET("/v2/public/tickers?symbol="+nfo.name)[0];
		double bid = resp["bid_price"].getNumber();
		double ask = resp["ask_price"].getNumber();
		double last = resp["last_price"].getNumber();
		if (nfo.invert_price)
			return {1.0/ask, 1.0/bid, 1.0/last, curTime.getCurTime()};
		else
			return {bid,ask,last,curTime.getCurTime()};
	}
}

void ByBitBroker::onInit() {

}

IBrokerControl::AllWallets ByBitBroker::getWallet() {
	AllWallets wlt;
	Wallet w;
	w.walletId = "futures";
	for (const auto &x: symbols) {
		if (x.second.type == inverse_perpetual) {
			w.wallet.push_back({x.second.currency_symbol,getWalletState(x.second.currency_symbol)["equity"].getNumber()});
		}
	}
	{
		w.wallet.push_back({"USDT", getWalletState("USDT")["equity"].getNumber()});
	}
	wlt.push_back(w);
	{
		Wallet sw;
		getSpotBalance("");
		for (Value z: spotBalanceCache["balances"]) {
			w.wallet.push_back({z["coin"].getString(),z["total"].getNumber()});
		}
	}


	return wlt;


}

bool ByBitBroker::hasKeys() const {
	return !api_key.empty() && !api_secret.empty();
}

void ByBitBroker::updateSymbols() const {
	auto now = std::chrono::steady_clock::now();
	if (symbols_expiration>now) return;
	forceUpdateSymbols();
	symbols_expiration = now+std::chrono::minutes(15);
}

void ByBitBroker::handleError(json::Value err) {
	Value ret_code = err["ret_code"];
	Value ret_msg = err["ret_msg"];
	if (ret_code.defined()) {
		std::string msg(ret_code.toString().str());
		msg.push_back(' ');
		msg.append(ret_msg.getString());
		throw std::runtime_error(msg);
	} else {
		throw std::runtime_error(err.toString().str());
	}
}

static json::String convertJSON2formData(json::Value params) {
	return Value(json::array, params.begin(), params.end(), [](const Value &k){
		return Value(json::string,{k.getKey(),"=",k.toString()});
	}).join("&");
}

std::string ByBitBroker::signRequest(json::Value &params) {
	if (curTime.needSync()) {
			Value r = publicGET("/spot/v1/time");//synchronize time (done as side effect)
			Value tm = r["serverTime"];
			if (tm.defined()) {
				curTime.setCurTime(tm.getUIntLong());
			} else {
				throw std::runtime_error("unable to sync time");
			}
	}
	params.setItems({
		{"api_key", api_key},
		{"timestamp", curTime.getCurTime()},
	});
	auto signString = convertJSON2formData(params);
	unsigned char digest[100];
	unsigned int dsize = 100;
	HMAC(EVP_sha256(), api_secret.data(), api_secret.size(), reinterpret_cast<const unsigned char *>(signString.c_str()), signString.length(),digest,&dsize);
	std::string_view hexChars("0123456789abcdef");
	std::string sign;
	sign.reserve(100);
	for (unsigned int i = 0; i < dsize; i++) {
		unsigned char v = digest[i];
		sign.push_back(hexChars[v >> 4]);
		sign.push_back(hexChars[v & 0xF]);
	}
	return sign;
}

json::Value ByBitBroker::privateGET(std::string_view uri, json::Value params) {
	std::string sign = signRequest(params);
	json::String qr = convertJSON2formData(params);
	std::string url(uri);
	url.append("?").append(qr.str()).append("&sign=").append(sign);
	return publicGET(url);
}

json::Value ByBitBroker::privatePOST(std::string_view uri, json::Value params) {
	std::string sign = signRequest(params);
	try {
		params.setItems({{"sign",sign}});
		Value ret = api.POST(uri,params);;
		Value ret_code = ret["ret_code"];
		if (ret_code.getUInt() != 0) handleError(ret);
		else {
			Value tn = ret["time_now"];
			if (tn.defined()) {
				curTime.setCurTimeAvg(static_cast<std::uint64_t>(tn.getNumber()*1000), 8);
			}
		}
		return ret["result"];
	} catch (const HTTPJson::UnknownStatusException &ex) {
		handleException(ex);
		throw;
	}
}

json::Value ByBitBroker::privatePOSTSpot(std::string_view uri, json::Value params) {
	std::string sign = signRequest(params);
	try {
		json::String qr ({convertJSON2formData(params).str(),"&sign=",sign});
		Value ret = api.POST(uri,qr);
		Value ret_code = ret["ret_code"];
		if (ret_code.getUInt() != 0) handleError(ret);
		return ret["result"];
	} catch (const HTTPJson::UnknownStatusException &ex) {
		handleException(ex);
		throw;
	}
}
json::Value ByBitBroker::privateDELETESpot(std::string_view uri, json::Value params) {
	std::string sign = signRequest(params);
	try {
		json::String qr ({convertJSON2formData(params).str(),"&sign=",sign});
		Value ret = api.DELETE(uri,qr);
		Value ret_code = ret["ret_code"];
		if (ret_code.getUInt() != 0) handleError(ret);
		return ret["result"];
	} catch (const HTTPJson::UnknownStatusException &ex) {
		handleException(ex);
		throw;
	}
}

void ByBitBroker::handleException(const HTTPJson::UnknownStatusException &ex) {
	auto s = ex.response.getBody();
	try {
		Value r (Value::parse(s));
		handleError(r);
	} catch (const ParseError &er) {
	}
	throw std::runtime_error(std::to_string(ex.code)+" "+ex.message);
}

Value ByBitBroker::publicGET(std::string_view uri) const {


	try {
		Value ret = api.GET(uri);
		Value ret_code = ret["ret_code"];
		if (ret_code.getUInt() != 0) handleError(ret);
		else {
			Value tn = ret["time_now"];
			if (tn.defined()) {
				curTime.setCurTimeAvg(static_cast<std::uint64_t>(tn.getNumber()*1000), 8);
			}
		}
		return ret["result"];
	} catch (const HTTPJson::UnknownStatusException &ex) {
		handleException(ex);
		throw;
	}
}

void ByBitBroker::forceUpdateSymbols() const {
	Value smblist = publicGET("/v2/public/symbols");
	SymbolMap::Set::VecT symbolvect;
	for (Value smb: smblist) {
		std::string_view name = smb["name"].getString();
		bool inverted = isInverted(name);
		MarketInfoEx nfo ;
		nfo.asset_step = smb["lot_size_filter"]["qty_step"].getNumber();
		nfo.min_size = smb["lot_size_filter"]["min_trading_qty"].getNumber();
		nfo.min_volume = 0;
		nfo.currency_step = smb["price_filter"]["tick_size"].getNumber();
		nfo.feeScheme = currency;
		nfo.fees = 0;
		nfo.leverage = smb["leverage_filter"]["max_leverage"].getNumber();
		nfo.private_chart = false;
		nfo.simulator = testnet;
		nfo.wallet_id = "futures";
		nfo.name = name;
		nfo.alias = smb["alias"].getString();
		nfo.expiration = extractExpiration(nfo.alias);
		if (inverted) {
			nfo.asset_symbol = smb["quote_currency"].getString();
			nfo.currency_symbol = smb["base_currency"].getString();
			nfo.invert_price = true;
			nfo.inverted_symbol = nfo.asset_symbol;
			nfo.type = nfo.expiration.empty()?inverse_perpetual:inverse_futures;
		} else {
			nfo.asset_symbol = smb["base_currency"].getString();
			nfo.currency_symbol = smb["quote_currency"].getString();
			nfo.invert_price = false;
			nfo.type = usdt_perpetual;
		}
		symbolvect.push_back(SymbolMap::value_type{
			std::string("f_").append(name),std::move(nfo)
		});
	}
	smblist = publicGET("/spot/v1/symbols");
	for (Value smb: smblist) {
		std::string_view name = smb["name"].getString();
		MarketInfoEx nfo ;
		nfo.name = name;
		nfo.alias = smb["alias"].getString();
		nfo.asset_symbol = smb["baseCurrency"].getString();
		nfo.currency_symbol = smb["quoteCurrency"].getString();
		nfo.asset_step = smb["basePrecision"].getNumber();
		nfo.currency_step = smb["minPricePrecision"].getNumber();
		nfo.min_size = smb["minTradeQuantity"].getNumber();
		nfo.min_volume = smb["minTradeQuantity"].getNumber();
		nfo.feeScheme = currency;
		nfo.fees = 0;
		nfo.leverage = 0;
		nfo.private_chart = false;
		nfo.simulator = testnet;
		nfo.wallet_id = "spot";
		nfo.type = spot;
		symbolvect.push_back(SymbolMap::value_type{
			std::string("s_").append(name),std::move(nfo)
		});
	}

	symbols = SymbolMap(std::move(symbolvect));


}

bool ByBitBroker::isInverted(const std::string_view &name) const {
	return name.substr(name.size()-4) != "USDT"; //no other way how to detect inversed symbol
}

const ByBitBroker::MarketInfoEx& ByBitBroker::getSymbol(const std::string_view &name) const {
	updateSymbols();
	auto iter = symbols.find(name);
	if (iter == symbols.end()) throw std::runtime_error(std::string("Unknown symbol: ").append(name));
	else return iter->second;
}

json::Value ByBitBroker::getInversePerpetualPosition(std::string_view symbol) {
	auto iter = positionCache.find(symbol);
	if (iter == positionCache.end()) {
		Value r = privateGET("/v2/private/position/list", Object({{"symbol",symbol}}));
		positionCache.emplace(std::string(symbol),r);
		return r;
	} else {
		return iter->second;
	}
}
json::Value ByBitBroker::getUSDTPerpetualPosition(std::string_view symbol) {
	auto iter = positionCache.find(symbol);
	if (iter == positionCache.end()) {
		Value r = privateGET("/private/linear/position/list", Object({{"symbol",symbol}}));
		positionCache.emplace(std::string(symbol),r);
		return r;
	} else {
		return iter->second;
	}
}
json::Value ByBitBroker::getInverseFuturePosition(std::string_view symbol) {
	auto iter = positionCache.find(symbol);
	if (iter == positionCache.end()) {
		Value r = privateGET("/futures/private/position/list", Object({{"symbol",symbol}}));
		positionCache.emplace(std::string(symbol),r);
		return r;
	} else {
		return iter->second;
	}
}

json::Value ByBitBroker::getWalletState(std::string_view symbol) {
	auto iter = walletCache.find(symbol);
	if (iter == walletCache.end()) {
		Value r = privateGET("/v2/private/wallet/balance", Object({{"coin",symbol}}))[0];
		walletCache.emplace(std::string(symbol),r);
		return r;
	} else {
		return iter->second;
	}
}

json::Value ByBitBroker::getSpotBalance(std::string_view coin) {
	if (!spotBalanceCache.defined()) {
		spotBalanceCache = privateGET("/spot/v1/account",json::object);
	}
	return spotBalanceCache["balances"].find([&](Value z){
		return z["coin"].getString() == coin;
	});

}
