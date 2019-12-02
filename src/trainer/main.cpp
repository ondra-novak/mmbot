/*
 * proxy.cpp
 *
 *  Created on: 19. 10. 2019
 *      Author: ondra
 */

#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <queue>
#include <unordered_set>

#include <imtjson/value.h>
#include "../brokers/api.h"
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>
#include <simpleServer/http_client.h>
#include "../brokers/httpjson.h"
#include "../shared/logOutput.h"

#include "../shared/stringview.h"

using json::Object;
using json::Value;
using json::String;
using ondra_shared::logDebug;
using ondra_shared::logError;
using ondra_shared::StrViewA;
using namespace simpleServer;


static Value setupForm = {};


static Value showIfAuto = Object
		("source",Value(json::array,{"cryptowatch"}));
static Value showIfUrl = Object
		("source",Value(json::array,{"urljson"}));
static Value showIfManual = Object
		("source",Value(json::array,{"manual"}));
static Value settingsForm = {
					Object
						("name","source")
						("type","enum")
						("label","Price source")
						("default","cryptowatch")
						("options",Object
								("cryptowatch","Cryptowatch")
								("urljson","Link to JSON url")
								("manual","Manual enter prices")
						),
					Object("name","src_asset")
						("type","enum")
						("label","Asset")
						("default","")
						("showif", showIfAuto)
						("options",Object
								("","---select---")
						),
					Object("name","src_currency")
							("type","enum")
							("label","Currency")
							("default","")
							("showif", showIfAuto)
							("options",Object
									("","---select---")
							),
					Object("name","src_url")
						("type","string")
						("label","URL")
						("default","")
						("showif", showIfUrl),
						Object("name","src_field")
							("type","string")
							("label","Field name")
							("default","")
							("showif", showIfUrl),
						Object
							("name","prices")
							("type","textarea")
							("label","Prices one per line\nor\nURL to JSON source,\nand name of the field at second line")
							("showif", showIfManual)
							("default",""),

						Object
							("name","timeframe")
							("type","number")
							("label","Time frame in minutes")
							("showif", showIfManual)
							("default",1),
						Object
							("name","asset")
							("type","string")
							("label","Asset symbol")
							("showif", Object("source", {"manual","urljson"}))
							("default","TEST"),
						Object
							("name","asset_balance")
							("type","number")
							("label","Asset Balance")
							("default","0"),
						Object
							("name","asset_step")
							("type","number")
							("label","Asset Step")
							("default","0"),
						Object
							("name","currency")
							("type","string")
							("label","Currency symbol")
							("showif", Object("source", {"manual","urljson"}))
							("default","FIAT"),
						Object
							("name","currency_balance")
							("type","number")
							("label","Currency Balance")
							("default","0"),
						Object
							("name","currency_step")
							("type","number")
							("label","Currency Step")
							("default","0"),
						Object
							("name","type")
							("type","enum")
							("options",Object
									("normal","Standard exchange")
									("futures","Normal futures")
									("inverted","Inverted futures")
									("futures_liq","Futures with liquidation")
									("inverted_liq","Inv Futures with liquidation"))
							("label","Market type")
							("default","normal"),
						Object
							("name","liq")
							("type","string")
							("label","Liquidation price")
							("showif",Object("type",{"futures_liq","inverted_liq"})),
						Object
							("name","restart")
							("type","enum")
							("showif", showIfManual)
							("options",Object
									("cont","Continue in chart")
									("restart","Restart from beginning"))
							("label","Restart or continue in chart")
							("default","cont"),
};


static std::size_t genIDCnt() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
}

class Interface: public AbstractBrokerAPI {
public:

	Interface(const std::string &path):AbstractBrokerAPI(path, setupForm),fname(path+".jconf"),idcnt(genIDCnt()) {}

	virtual BrokerInfo getBrokerInfo()  override;
	virtual void onLoadApiKey(json::Value) override {}

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
	virtual void onInit() override;
	virtual void setSettings(json::Value v) override;
	virtual json::Value getSettings(const std::string_view &) const override ;
	virtual PageData fetchPage(const std::string_view &method, const std::string_view &vpath, const PageData &pageData) override;


	json::Value collectSettings() const;
	void saveSettings();
	void loadSettings();
	void updateLiq(double openPrice);

	void unsuppError() {
		throw std::runtime_error("Unsupported operation - The trainer must be run with 'dry_run' flag enabled");
	}

	bool inited;
	time_t startTime = 0;
	std::vector<double> prices;
	long timeDivisor = 120;
	std::string asset = "TEST";
	double asset_balance = 0;
	double asset_step = 0;
	std::string currency = "FIAT";
	double currency_balance = 0;
	double currency_step = 0;
	bool futures = false;
	bool inverted = false;
	bool liquidation = false;
	double prev_price = 0;
	double low_liq = 0;
	double high_liq = std::numeric_limits<double>::max();


	Orders orders;
	TradeHistory trades;

	std::string fname;
	std::size_t idcnt;

	mutable double last_price = 0;
	double getCurPrice() const;
	std::string price_url;
	std::string price_path;
	std::string price_source = "manual";
	std::string src_asset;
	std::string src_currency;

};


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Required storage path" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();
}

struct CWPairs {
	std::vector<std::string> assets;
	std::vector<std::string> currencies;
};

static CWPairs getCWAssets() {
	static CWPairs cache;
	static std::chrono::system_clock::time_point cacheExpire;
	auto now= std::chrono::system_clock::now();
	if (cacheExpire < now) {

		try {
			HTTPJson httpc(simpleServer::HttpClient("MMBot Trainer",newHttpsProvider(), newNoProxyProvider()),"");
			json::Value v = httpc.GET("https://api.cryptowat.ch/pairs");

			std::unordered_set<std::string> assets, currencies;
			for (json::Value r : v["result"]) {
				String asset = r["base"]["symbol"].toString();
				String currency = r["quote"]["symbol"].toString();
				assets.insert(asset.str());
				currencies.insert(currency.str());
			}
			cache =  {
				std::vector<std::string>(assets.begin(), assets.end()),
				std::vector<std::string>(currencies.begin(), currencies.end())
			};

		} catch (std::exception &e) {
			logError("$1",e.what());
		}
		cacheExpire = now+std::chrono::hours(1);
	}

	return cache;
}


static std::string createCWUrl(const std::string &asset, const std::string &currency) {
	static std::unordered_map<std::string, double> volumes;
	HTTPJson httpc(simpleServer::HttpClient("MMBot Trainer",newHttpsProvider(), newNoProxyProvider()),"");

	if (volumes.empty()) {
		json::Value v = httpc.GET("https://api.cryptowat.ch/markets/summaries");
		for (Value z:v["result"]) {
			volumes[z.getKey()] = z["volume"].getNumber();
		}
	}

	auto getScore = [&](Value v) {
		std::string key (v["exchange"].getString());
		key.push_back(':');
		key.append(v["pair"].getString());
		auto iter = volumes.find(key);
		double res =( iter != volumes.end()?iter->second:0.0);
		logDebug("Exchange $1 score $2", key, res);
		return res;
	};

	json::Value v = httpc.GET("https://api.cryptowat.ch/pairs");



	Value mk = v["result"].find([asset =StrViewA(asset), currency = StrViewA(currency)](Value v) {
		String a = v["base"]["symbol"].toString();
		String c = v["quote"]["symbol"].toString();
		return (a == asset && c == currency);
	});
	if (mk.defined()) {
			String rt = mk["route"].toString();
			json::Value m = httpc.GET(rt.str());
			json::Value markets = m["result"]["markets"].filter(
					[&](Value v){return v["active"].getBool();}
			).sort([&](Value a, Value b) {
				return getScore(b) - getScore(a);
			});
			return std::string(markets[0]["route"].getString())+"/price";
	}
	return std::string();
}

inline Interface::BrokerInfo Interface::getBrokerInfo() {
	return BrokerInfo {
		true,
		"Trainer",
		"Trainer",
		"/",
		"1.0",

		"Trainer(c) 2019 Ondřej Novák\n\n"
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

		"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"
		"AAAASUVORK5CYII=",
		true
	};
}


inline double Interface::getBalance(const std::string_view &x) {
	if (inverted) {
		if (x == "CONTRACT") return asset_balance;
		else return currency_balance;
	} else {
		if (x == currency) return currency_balance;
		else return asset_balance;
	}
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	using namespace json;
	if (lastId.hasValue()) {
		TradeHistory ret;
		std::copy_if(trades.begin(), trades.end(), std::back_inserter(ret), [&](const Trade &x) {
			return Value::compare(x.id, lastId) > 0;
		});
		return TradesSync{ret, trades.empty()? lastId: trades.back().id};
	} else {
		return TradesSync { {}, trades.empty()? Value(nullptr): trades.back().id};
	}
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &par) {
	return orders;
}

static Value searchField(Value data, StrViewA path) {
	auto n = path.indexOf(".");
	StrViewA f;
	StrViewA r;
	if (n == path.npos) {
		f = path;
	} else {
		f = path.substr(0,n);
		r = path.substr(n+1);
	}

	n = f.indexOf("[");
	StrViewA idxs;
	if (n != f.npos) {
		idxs = f.substr(n+1);
		f = f.substr(0,n);
	}

	Value found;


	if (f.empty()) {
		found = data;
	}
	else  {
		std::queue<Value> q;

		for (Value n : data) {
			if (n.getKey() == f) {
				found = n;
				break;
			}
			q.push(n);
		}
		if (!found.defined()) {
			while (!q.empty()) {
				Value n = q.front();
				q.pop();
				if (n.getKey() == f) {
					found = n;
					break;
				}
				for (Value m: n) q.push(m);
			}
		}
	}
	if (!found.defined()) return found;
	if (!idxs.empty()) {
		auto splt = idxs.split("][");
		while (!!splt && found.isContainer()) {
			StrViewA idx = splt();
			auto i = std::strtod(idx.data,nullptr);
			found = found[i];
		}
	}
	if (found.type() == json::object && !r.empty()) {
		return searchField(found,r);
	} else {
		return found;
	}

}

double Interface::getCurPrice() const {
	if (last_price) return last_price;
	double price = 0;

	if (price_source == "urljson" || price_source == "cryptowatch") {
		if (!price_url.empty()) {
			HTTPJson httpc(simpleServer::HttpClient("MMBot Trainer",newHttpsProvider(), newNoProxyProvider()),"");
			json::Value resp = httpc.GET(price_url);
			Value found = searchField(resp,price_path);
			price = found.getNumber();
		}
	} else {
		if (prices.empty()) return 0;

		time_t t = time(nullptr) - startTime;
		std::size_t index = (t/timeDivisor) % prices.size();
		price = prices[index];
	}
	if (price == 0) return 0;
	if (inverted) price = 1/price;
	last_price = price;
	return price;

}

inline Interface::Ticker Interface::getTicker(const std::string_view &piar) {
	double price = getCurPrice();
	if (price == 0)
		 throw std::runtime_error("Trainer: Failed to get current price, check configuration");

	return Ticker{price,price,price,uintptr_t(time(nullptr))*1000};
}

inline json::Value Interface::placeOrder(const std::string_view &,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {


	double p = getCurPrice();
	auto iter = std::find_if(orders.begin(), orders.end(),[&](const Order &o) {
		return o.id == replaceId;
	});
	if (iter != orders.end()) {
		if (replaceSize > fabs(iter->size)) return iter->id;
		else orders.erase(iter);
	}

	if (price > high_liq || price < low_liq) {
		throw std::runtime_error("Balance is low");
	}

	Value id = idcnt++;
	if (size) {

		if (((p - price) / size) < 0) price = p;

		orders.push_back(Order{
			id,clientId,
			size,price
		});
	}

	return id;
}

inline void Interface::updateLiq(double openPrice) {
	if (liquidation && asset_balance) {
		if (currency_balance<0) {
			high_liq = 0;
			low_liq = std::numeric_limits<double>::max();
		} else if (asset_balance < 0) {
			high_liq = -currency_balance/asset_balance + openPrice;
			low_liq = 0;
		} else if (asset_balance > 0) {
			low_liq = -currency_balance/asset_balance + openPrice;
			high_liq = std::numeric_limits<double>::max();
		} else {
			low_liq = 0;
			high_liq = std::numeric_limits<double>::max();
		}

	} else {
		low_liq = 0;
		high_liq = std::numeric_limits<double>::max();
	}
	logDebug("Liquidation prices: High $1, Low $2", inverted?1.0/low_liq:high_liq, inverted?1.0/high_liq:low_liq);
}

inline bool Interface::reset() {

	last_price = 0;
	double p = getCurPrice();

	if (p < low_liq || p > high_liq) {
		Value id = ++idcnt;
		Trade tr {
			id,
			std::size_t(time(nullptr)*1000),
			-asset_balance,
			p,
			-asset_balance,
			p,
		};
		trades.push_back(tr);
		currency_balance = 0;
		asset_balance = 0;
		orders.clear();
	}


	Orders newOrders;
	for (auto o : orders) {
		double dp = p - o.price;
		if (dp / o.size <= 0) {
			double pprice = trades.empty()?p:trades.back().price;
			Value id = ++idcnt;
			double s = o.size * (dp == 0?0.5:1);
			Trade tr {
				id,
				std::size_t(time(nullptr)*1000),
				s,
				o.price,
				s,
				o.price
			};
			trades.push_back(tr);
			if (futures) {
				currency_balance += asset_balance*(o.price - pprice);
			}
			asset_balance += s;
			if (!futures) {
				currency_balance -= s * o.price;
			}
			double remain = (o.size - s);
			if (std::fabs(remain) > (asset_step+1e-20)) {
				newOrders.push_back(Order {
					o.id,
					o.client_id,
					remain,
					o.price
				});
			}
			updateLiq(o.price);
		} else {
			newOrders.push_back(o);
		}
	}

	newOrders.swap(orders);
	prev_price = p;
	saveSettings();
	getCWAssets();
	return true;

}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	return MarketInfo{
		inverted?"CONTRACT":asset,
		inverted?asset:currency,
		asset_step,
		currency_step,
		asset_step,
		0,
		0,
		AbstractBrokerAPI::currency,
		futures?1000000.0:0.0,
		inverted,
		currency,
		true
	};
}


inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

inline std::vector<std::string> Interface::getAllPairs() {
	return {"TRAINER_PAIR"};
}


inline void Interface::onInit() {
	loadSettings();
}

inline void Interface::setSettings(json::Value keyData) {
	timeDivisor = keyData["timeframe"].getInt()*60;
	prices.clear();
	price_url.clear();
	price_path.clear();

	StrViewA textPrices = keyData["prices"].getString().trim(isspace);
	{
		auto splt = textPrices.split("\n");
		while (!!splt) {
			StrViewA line = splt();
			line = line.trim(isspace);
			if (!line.empty()) {
				double d = strtod(line.data,0);
				if (std::isfinite(d) && d > 0) {
					prices.push_back(d);
				}
			}
		}
	}
	Value st = keyData["startTime"];
	if (st.hasValue()) startTime = st.getUInt();
	else {
		if (keyData["restart"].getString() == "restart") {
			startTime = time(nullptr);
		}
	}

	asset = keyData["asset"].getString();
	currency = keyData["currency"].getString();
	asset_balance = keyData["asset_balance"].getNumber();
	currency_balance = keyData["currency_balance"].getNumber();
	asset_step = keyData["asset_step"].getNumber();
	currency_step = keyData["currency_step"].getNumber();
	price_source = keyData["source"].getString();
	price_path = keyData["src_field"].getString();
	price_url = keyData["src_url"].getString();
	src_asset = keyData["src_asset"].getString();
	src_currency = keyData["src_currency"].getString();
	futures = false;
	inverted = false;
	liquidation = false;
	StrViewA type = keyData["type"].getString();
	if (type == "futures") {
		futures = true;
	} else if (type == "inverted") {
		futures = true;
		inverted = true;
	} else if (type == "futures_liq") {
		futures = true;
		liquidation = true;
	} else if (type == "inverted_liq") {
		futures = true;
		liquidation = true;
		inverted = true;
	}
	if (price_source == "cryptowatch") {
		std::string url = createCWUrl(src_asset, src_currency);
		if (url.empty()) throw std::runtime_error("Unable to find market for selected combination");
		asset = src_asset;
		currency = src_currency;
		std::transform(asset.begin(), asset.end(), asset.begin(), toupper);
		std::transform(currency.begin(), currency.end(), currency.begin(), toupper);
		price_url = url;
		price_path = "price";

	}
	prev_price = keyData["prev_price"].getNumber();
	saveSettings();
	updateLiq(prev_price);
}

json::Value Interface::collectSettings() const {
	json::Object kv;
	kv.set("prices",json::Value(json::array, prices.begin(), prices.end(), [](double v){return v;}).join("\n"))
		  ("asset",asset)
		  ("currency",currency)
		  ("asset_balance",asset_balance)
		  ("currency_balance",currency_balance)
		  ("asset_step",asset_step)
		  ("currency_step",currency_step)
		  ("type",futures?
				  	  (inverted?
						  (liquidation?"inverted_liq":"inverted")
						  :(liquidation?"futures_liq":"futures"))
					  :("normal"))
		  ("startTime",startTime)
		  ("restart","cont")
		  ("timeframe",timeDivisor/60)
		  ("prev_price", prev_price)
		  ("source", price_source)
		  ("src_url", price_url)
		  ("src_field", price_path)
	  	  ("src_asset", src_asset)
	  	  ("src_currency", src_currency);
	return kv;
}


json::Value mergeAssets(Value options, const std::vector<std::string> &a) {
	Object o(options);
	for (auto &&k : a) o.set(k,k);
	return o;
}

inline json::Value Interface::getSettings(const std::string_view & ) const {
	Value kv = collectSettings();
	CWPairs cwAssets = getCWAssets();

	return settingsForm.map([&](Value v) {
		StrViewA n = v["name"].getString();
		Value z = v.replace("default", kv[n]);
		if (z["name"].getString() == "src_asset") {
			z = z.replace("options", mergeAssets(z["options"], cwAssets.assets));
		}
		else if (z["name"].getString() == "src_currency") {
			z = z.replace("options", mergeAssets(z["options"], cwAssets.currencies));
		} else if (z["name"].getString() == "liq") {
			if (asset_balance > 0) {
				z = z.replace("default",inverted?1.0/low_liq:high_liq);
			} else if (asset_balance < 0) {
				z = z.replace("default",inverted?1.0/high_liq:low_liq);
			}
		}
		return z;
	});
}

inline void Interface::saveSettings() {
	std::ofstream f(fname, std::ios::out|std::ios::trunc);
	collectSettings().toStream(f);
}

inline void Interface::loadSettings() {
	std::ifstream f(fname);
	if (!f) return;
	Value v = Value::fromStream(f);
	if (!f) return;
	setSettings(v);
}

Interface::PageData Interface::fetchPage(const std::string_view &method, const std::string_view &vpath, const Interface::PageData &pageData) {

	std::ostringstream out;
	out << R"html(
		<!DOCTYPE html>
		<html>
		<head><title>Trainer example web page</title><head>
		<body>
		<h1>Trainer</h1>
        <p>Hello - this is example page of the trainer. It is also test of the function fetchPage. Following part of the page contains data of the request.</p><table border="1">)html";
	out << "<tr><th>Method:<td>" << method;
	out << "<tr><th>Path:<td>" << vpath;
	for (auto &&k: pageData.headers) {
		out << "<tr><th>" << k.first << "<td>" << k.second;
	}
	out << "<tr><th>Body:<td>" << pageData.body;
	out << "</table>";
	out << "</body></html>";

	return Interface::PageData {
		200,
		{
				{"Content-Type","text/html;charset=utf-8"}
		},
		out.str()
	};
}
