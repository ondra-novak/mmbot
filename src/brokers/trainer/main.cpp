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
#include <random>
#include <unordered_set>

#include <imtjson/value.h>
#include "../api.h"
#include <imtjson/object.h>
#include <imtjson/string.h>
#include <imtjson/operations.h>
#include <simpleServer/http_client.h>
#include "../httpjson.h"
#include <shared/logOutput.h>

#include <shared/stringview.h>
#include "../bitfinex/structs.h"

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
static Value showIfBfx= Object
		("source",Value(json::array,{"bitfinex"}));
static Value showIfUrl = Object
		("source",Value(json::array,{"urljson"}));
static Value showIfManual = Object
		("source",Value(json::array,{"manual"}));
static Value settingsForm = {
					Object
						("name","pair")
						("type","string")
						("label","Pair")
						("showif",json::array),
					Object
						("name","source")
						("type","enum")
						("label","Price source")
						("default","cryptowatch")
						("options",Object
								("cryptowatch","Cryptowatch")
								("bitfinex","Bitfinex")
								("urljson","Link to JSON url")
								("manual","Manual enter prices")
								("orders","Execute orders")
								("randomwalk","Random walk")
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
					Object("name","bfx_src")
						("type","enum")
						("label","Symbol")
						("default","")
						("showif", showIfBfx)
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
							("label","Prices - one per line")
							("showif", showIfManual)
							("default",""),

						Object
							("name","timeframe")
							("type","number")
							("label","Time frame in minutes")
							("showif", Object("source",{"manual","randomwalk"}))
							("default",1),
						Object
							("name","asset")
							("type","string")
							("label","Asset symbol")
							("showif", Object("source", {"manual","urljson","orders","randomwalk"}))
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
							("showif", Object("source", {"manual","urljson","orders","randomwalk"}))
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

static Value presetSettingsForm = {
		Object
			("name","pair")
			("type","string")
			("label","Pair")
			("showif",json::array),
		Object
			("name","asset_balance")
			("type","number")
			("default","0")
			("label","Assets"),
		Object
			("name","currency_balance")
			("type","number")
			("default","0")
			("label","Currency"),
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
};

static std::size_t genIDCnt() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
}

static HTTPJson httpc(simpleServer::HttpClient("MMBot Trainer",newHttpsProvider(), newNoProxyProvider()),"");


class Interface: public AbstractBrokerAPI {
public:

	Interface(const std::string &path):AbstractBrokerAPI(path, setupForm),fname(path+".jconf"),idcnt(genIDCnt()) {}

	virtual BrokerInfo getBrokerInfo()  override;
	virtual void onLoadApiKey(json::Value) override {}

	virtual double getBalance(const std::string_view & symb) override;
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
	virtual void onInit() override;
	virtual json::Value setSettings(json::Value v) override;
	virtual void restoreSettings(json::Value v) override;
	void setSettings(json::Value v, bool loaded, unsigned int pairId) ;
	virtual json::Value getSettings(const std::string_view &) const override ;
	virtual PageData fetchPage(const std::string_view &method, const std::string_view &vpath, const PageData &pageData) override;


	class TestPair {
	public:


		json::Value collectSettings() const;

		bool inited = false;
		bool preset = false;
		time_t startTime = 0;
		time_t activityTime = 0;
		int activityCounter = 10;
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
		mutable double last_fetch_price = 0;
		mutable std::chrono::steady_clock::time_point last_fetch_price_exp;
		mutable double evodd_price = 1000;
		mutable time_t evodd_time = 0;
		mutable bool evodd_swap = false;


		Orders orders;
		TradeHistory trades;


		mutable double last_price = 0;
		mutable double last_order_price = 0;
		double getCurPrice() const;
		std::string price_url;
		std::string price_path;
		std::string price_source = "manual";
		std::string src_asset;
		std::string src_currency;
		void updateLiq(double openPrice);
		void addTrade(std::size_t &idcnt, double price, double size);
		void init(const StrViewA &name);
		void updateActivity();
	};


	using PairMap = std::unordered_map<unsigned int, TestPair>;
	PairMap pairs;

	Value saveSettings();
	void loadSettings();

	TestPair &getPair(const std::string_view &name);
	const TestPair *getPairPtr(const std::string_view &name) const;


	std::string fname;
	std::size_t idcnt;

	virtual json::Value getMarkets() const;
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

class BitfinexSource {
public:
	BitfinexSource(HTTPJson &httpjson):httpc(httpjson) {}

	mutable PairList pairList;

	const PairList& getPairs() const;
	mutable std::chrono::system_clock::time_point pairListExpire;
	double getPrice(const std::string_view &tsymbol);
	std::string getPriceURL(const std::string_view &tsymbol);

	HTTPJson &httpc;

};

const PairList& BitfinexSource::getPairs() const {
	auto now = std::chrono::system_clock::now();
	if (now < pairListExpire) return pairList;
	try {
		auto r = httpc.GET("https://api.bitfinex.com/v2/conf/pub:info:pair");
		pairList =readPairs(r);
		pairListExpire = now+std::chrono::hours(1);
		return pairList;
	} catch (...) {
		pairListExpire = now;
		return pairList;
	}
}


class CWSource {
public:

	struct Pairs {
		std::vector<std::string> assets;
		std::vector<std::string> currencies;
	};

	std::unordered_map<std::string, json::Value>cache;
	std::unordered_map<std::string, double> volumes;
	std::chrono::system_clock::time_point expires;
	HTTPJson &httpc;


	CWSource(HTTPJson &httpc)
		:expires(std::chrono::system_clock::now())
		,httpc(httpc) {}

	void cleanIfExpired() {
		auto now = std::chrono::system_clock::now();
		if (now > expires) {
			cache.clear();
			volumes.clear();
			expires = now+std::chrono::hours(1);
		}
	}

	json::Value getCached(const std::string &url) {
		auto iter = cache.find(url);
		if (iter == cache.end()) {
			json::Value data = httpc.GET(url);
			cache.emplace(url,data);
			return data;
		} else {
			return iter->second;
		}
	}

	json::Value getPrices(StrViewA a, StrViewA c, unsigned int days, unsigned int period) {
		std::string url = createUrl(a, c,"ohlc");
		if (url.empty()) throw std::runtime_error("Cannot find market for given pair");
		time_t tbeg = time(nullptr)-86400*days;
		std::string p = std::to_string(period);
		url  = url+"?after="+std::to_string(tbeg)+"&periods="+p;
		Value resp = httpc.GET(url);
		Value result = resp["result"];
		return result[p].map([](Value v){return v[4];});
	}

	Pairs getAssets() {

		try {
			json::Value v = getCached("https://api.cryptowat.ch/pairs");

			std::unordered_set<std::string> assets, currencies;
			for (json::Value r : v["result"]) {
				String asset = r["base"]["symbol"].toString();
				String currency = r["quote"]["symbol"].toString();
				assets.insert(asset.str());
				currencies.insert(currency.str());
			}
			return {
				std::vector<std::string>(assets.begin(), assets.end()),
				std::vector<std::string>(currencies.begin(), currencies.end())
			};

		} catch (std::exception &e) {
			logError("$1",e.what());
			return {};
		}
	}

	double getScore(Value v) {
		std::string key (v["exchange"].getString());
		key.push_back(':');
		key.append(v["pair"].getString());
		auto iter = volumes.find(key);
		double res =( iter != volumes.end()?iter->second:0.0);
		logDebug("Exchange $1 score $2", key, res);
		return res;
	};


	std::string createUrl(const std::string &asset, const std::string &currency, const std::string &endpoint) {
		if (volumes.empty()) {
			json::Value v = getCached("https://api.cryptowat.ch/markets/summaries");
			for (Value z:v["result"]) {
				volumes[z.getKey()] = z["volume"].getNumber();
			}
		}

		json::Value v = getCached("https://api.cryptowat.ch/pairs");


		Value mk = v["result"].find([asset =StrViewA(asset), currency = StrViewA(currency)](Value v) {
			String a = v["base"]["symbol"].toString();
			String c = v["quote"]["symbol"].toString();
			return (a == asset && c == currency);
		});
		if (mk.defined()) {
				String rt = mk["route"].toString();
				json::Value m = getCached(rt.str());
				json::Value markets = m["result"]["markets"].filter(
						[&](Value v){return v["active"].getBool();}
				).sort([&](Value a, Value b) {
					return getScore(b) - getScore(a);
				});
				return std::string(markets[0]["route"].getString())+"/"+endpoint;
		}
		return std::string();
	}

};

static CWSource cwsource(httpc);
static BitfinexSource bfxsource(httpc);




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
	return 0;
}
double Interface::getBalance(const std::string_view & symb, const std::string_view & pair) {

	TestPair &p = getPair(pair);
	if (p.inverted) {
		if (symb == "CONTRACT") return p.asset_balance;
		else return p.currency_balance;
	} else {
		if (symb == p.currency) return p.currency_balance;
		else return p.asset_balance;
	}
}

inline Interface::TradesSync Interface::syncTrades(json::Value lastId, const std::string_view &pair) {
	TestPair &p = getPair(pair);

	using namespace json;
	if (lastId.hasValue()) {
		TradeHistory ret;
		std::copy_if(p.trades.begin(), p.trades.end(), std::back_inserter(ret), [&](const Trade &x) {
			return Value::compare(x.id, lastId) > 0;
		});
		return TradesSync{ret, p.trades.empty()? 0: p.trades.back().id};
	} else {
		return TradesSync { {}, p.trades.empty()? 0: p.trades.back().id};
	}
}

inline Interface::Orders Interface::getOpenOrders(const std::string_view &pair) {
	TestPair &p = getPair(pair);
	return p.orders;
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

static std::mt19937 rndgen((std::random_device()()));

double Interface::TestPair::getCurPrice() const {
	if (last_price) return last_price;
	double price = 0;
	auto now = std::chrono::steady_clock::now();

	if (price_source == "urljson" || price_source == "cryptowatch" || price_source == "bitfinex") {
		if (!price_url.empty()) {
			if (now > last_fetch_price_exp) {
				last_fetch_price_exp = now + std::chrono::seconds(30);
				json::Value resp = httpc.GET(price_url);
				Value found = searchField(resp,price_path);
				price = found.getNumber();
				last_fetch_price = price;
			} else {
				price = last_fetch_price;
			}
		}
	} else if (price_source == "randomwalk") {
		time_t t = time(nullptr);
		if ((t- evodd_time) > timeDivisor) {
			double pp = evodd_price;
			if (pp == 0) pp = 100;
			std::uniform_int_distribution<> unif(0,1);
			auto numb = unif(rndgen);
			auto v = numb == 0;
			double bet = std::round(pp * 0.01);
			if (v == evodd_swap) evodd_price = std::round(pp+bet); else evodd_price = std::round(pp-bet);
			std::cerr << "[bet] numb=" << numb << ",side=" << ((v == evodd_swap)?"T":"F") << ",bet=" << bet << ", swap=" << (evodd_swap?"T":"F") << std::endl;
			evodd_swap = !evodd_swap;
			evodd_time = t;
		}
		price = evodd_price;
	} else if (price_source == "orders") {
		return last_order_price;
	} else {
		if (prices.empty()) return 0;

		time_t t = time(nullptr) - startTime;
		std::size_t index = (t/timeDivisor) % prices.size();
		price = prices[index];
	}
	if (price == 0) return 0;
	if (inverted) price = 1/price;
	last_price = price;
	last_order_price = price;
	return price;

}

inline Interface::Ticker Interface::getTicker(const std::string_view &pair) {
	TestPair &p = getPair(pair);
	double price = p.getCurPrice();
	if (price == 0) {
		if (p.price_source == "orders") {
				throw std::runtime_error("No last price defined - execute first order");
		} else {
			throw std::runtime_error("Failed to get current price, check configuration");
		}
	}

	return Ticker{price,price,price,uintptr_t(time(nullptr))*1000};
}

inline json::Value Interface::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	TestPair &p = getPair(pair);


	double cp = p.getCurPrice();
	auto iter = std::find_if(p.orders.begin(), p.orders.end(),[&](const Order &o) {
		return o.id == replaceId;
	});
	if (iter != p.orders.end()) {
		if (replaceSize > fabs(iter->size)) return iter->id;
		else p.orders.erase(iter);
	}

	if (price > p.high_liq || price < p.low_liq) {
		throw std::runtime_error("Balance is low");
	}

	Value id = idcnt++;
	if (p.price_source == "orders" && !clientId.hasValue()) {
		p.addTrade(idcnt, price, size);
	} else {

		if (size) {

			if (((cp - price) / size) < 0) price = cp;

			p.orders.push_back(Order{
				id,clientId,
				size,price
			});
		} else if (p.price_source == "orders") {
			p.last_order_price = price;
		}
	}
	return id;
}

inline void Interface::TestPair::updateLiq(double openPrice) {
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

void Interface::TestPair::addTrade(std::size_t &idcnt, double price, double size) {
	double pprice = trades.empty()?price:trades.back().price;
	Value id = ++idcnt;
	Trade tr {
		id,
		std::size_t(time(nullptr)*1000),
		size,
		price,
		size,
		price
	};
	trades.push_back(tr);
	if (futures) {
		currency_balance += asset_balance*(price - pprice);
	}
	asset_balance += size;
	if (!futures) {
		currency_balance -= size * price;
	}
	last_order_price = price;
}

inline bool Interface::reset() {

	time_t now = time(nullptr);
	time_t exp = now - 60;
	std::vector<std::size_t> remove;

	for (auto &&pp: pairs) {

		auto &p = pp.second;
		if (p.activityTime < exp) {
			p.activityCounter--;
			if (p.activityCounter<=0) {
				remove.push_back(pp.first);
				continue;
			} else {
				p.activityTime = now;
			}
		}


		double cp;
		double lp = p.last_price;
		p.last_price = 0;
		try {
			cp = p.getCurPrice();
		} catch (...) {
			cp = lp;
		}

		if (cp < p.low_liq || cp > p.high_liq) {
			Value id = ++idcnt;
			Trade tr {
				id,
				std::size_t(time(nullptr)*1000),
				-p.asset_balance,
				cp,
				-p.asset_balance,
				cp,
			};
			p.trades.push_back(tr);
			p.currency_balance = 0;
			p.asset_balance = 0;
			p.orders.clear();
		}


		Orders newOrders;
		for (auto o : p.orders) {
			double dp = cp - o.price;
			if (dp / o.size <= 0) {
				double s = o.size * (dp == 0?0.5:1);
				if (std::abs(s) < p.asset_step) s = o.size;
				double price = o.price;
				p.addTrade(idcnt, price, s);
				double remain = (o.size - s);
				if (std::fabs(remain) > (p.asset_step+1e-20)) {
					newOrders.push_back(Order {
						o.id,
						o.client_id,
						remain,
						o.price
					});
				}
				p.updateLiq(o.price);
			} else {
				newOrders.push_back(o);
			}
		}

		newOrders.swap(p.orders);
		p.prev_price = cp;
	}
	for (auto &&k : remove) {
		pairs.erase(k);
	}
	saveSettings();
	return true;

}

inline Interface::MarketInfo Interface::getMarketInfo(const std::string_view &pair) {
	auto &p = getPair(pair);
	return MarketInfo{
		p.inverted?"CONTRACT":p.asset,
		p.inverted?p.asset:p.currency,
		p.asset_step,
		p.currency_step,
		p.asset_step,
		0,
		0,
		AbstractBrokerAPI::currency,
		p.futures?1000000.0:0.0,
		p.inverted,
		p.currency,
		true
	};
}


inline double Interface::getFees(const std::string_view &pair) {
	return 0;
}

inline std::vector<std::string> Interface::getAllPairs() {
	unsigned int i = 0;
	static constexpr std::string_view testStr = "TEST";
	char pairName[40];

	do {
		++i;
		char *d = pairName;
		std::copy(testStr.begin(),testStr.end(), d);
		d+=4;
		ondra_shared::unsignedToString(i,[&d](char z){*d++=z;},10,4);
		*d = 0;
	} while (getPairPtr(pairName) != nullptr);

	std::vector<std::string> pairs;
	pairs.push_back(pairName);

	try {
		auto bfxpairs = bfxsource.getPairs();
		for (const auto &p : bfxpairs) {
			pairs.push_back(p.second.symbol.str());
		}
	} catch (...) {

	}
	return pairs;
}


inline void Interface::onInit() {
	loadSettings();
}

inline json::Value Interface::setSettings(json::Value keyData) {
		std::hash<std::string_view> h;
		setSettings(keyData,false,h(keyData["pair"].getString()));
		return saveSettings();
}
inline void Interface::restoreSettings(json::Value keyData) {
	for (Value x: keyData) {
		auto pair = x["pairId"].getUInt();
		if (pairs.find(pair) == pairs.end()) {
			setSettings(x,true,pair);
		}
	}
}
inline void Interface::setSettings(json::Value keyData, bool loaded,  unsigned int pairId) {
	auto &p = pairs[pairId];

	if (p.preset && !loaded) {
		keyData = p.collectSettings().merge(keyData);
	}

	p.activityTime = time(nullptr);
	p.activityCounter = 10;

	p.timeDivisor = keyData["timeframe"].getInt()*60;
	p.prices.clear();
	p.price_url.clear();
	p.price_path.clear();

	StrViewA textPrices = keyData["prices"].getString().trim(isspace);
	{
		auto splt = textPrices.split("\n");
		while (!!splt) {
			StrViewA line = splt();
			line = line.trim(isspace);
			if (!line.empty()) {
				double d = strtod(line.data,0);
				if (std::isfinite(d) && d > 0) {
					p.prices.push_back(d);
				}
			}
		}
	}
	Value st = keyData["startTime"];
	if (st.hasValue()) p.startTime = st.getUInt();
	else {
		if (keyData["restart"].getString() == "restart") {
			p.startTime = time(nullptr);
		}
	}

	p.asset = keyData["asset"].getString();
	p.currency = keyData["currency"].getString();
	p.asset_balance = keyData["asset_balance"].getNumber();
	p.currency_balance = keyData["currency_balance"].getNumber();
	p.asset_step = keyData["asset_step"].getNumber();
	p.currency_step = keyData["currency_step"].getNumber();
	p.price_source = keyData["source"].getString();
	p.price_path = keyData["src_field"].getString();
	p.price_url = keyData["src_url"].getString();
	p.src_asset = keyData["src_asset"].getString();
	p.src_currency = keyData["src_currency"].getString();
	p.activityCounter = keyData["activityCounter"].getValueOrDefault(10);
	p.futures = false;
	p.inverted = false;
	p.liquidation = false;
	StrViewA type = keyData["type"].getString();
	if (type == "futures") {
		p.futures = true;
	} else if (type == "inverted") {
		p.futures = true;
		p.inverted = true;
	} else if (type == "futures_liq") {
		p.futures = true;
		p.liquidation = true;
	} else if (type == "inverted_liq") {
		p.futures = true;
		p.liquidation = true;
		p.inverted = true;
	}
	if (p.price_source == "cryptowatch") {
		if (!keyData["loaded"].defined()) {
			std::string url = cwsource.createUrl(p.src_asset, p.src_currency,"price");
			if (url.empty()) throw std::runtime_error("Unable to find market for selected combination");
			p.asset = p.src_asset;
			p.currency = p.src_currency;
			std::transform(p.asset.begin(), p.asset.end(), p.asset.begin(), toupper);
			std::transform(p.currency.begin(), p.currency.end(), p.currency.begin(), toupper);
			p.price_url = url;
			p.price_path = "price";
		}
	} else if (p.price_source == "bitfinex") {
		if (!keyData["loaded"].defined()) {
			const auto &pairs = bfxsource.getPairs();
			auto name = keyData["bfx_src"].getString();
			auto iter = std::find_if(pairs.begin(), pairs.end(), [&](const auto &pair) {
				return pair.second.symbol == name;
			});
			if (iter != pairs.end()) {
				p.price_url = bfxsource.getPriceURL(iter->second.tsymbol.str());
				p.price_path = "[6]";
				p.asset = iter->second.asset.str();
				p.currency = iter->second.currency.str();
				p.asset_step = iter->second.min_size;
			} else {
					throw std::runtime_error("Unknown symbold");
			}
		}
	}
	p.last_order_price=keyData["last_order_price"].getValueOrDefault(p.last_order_price);
	p.prev_price = keyData["prev_price"].getNumber();
	p.preset = keyData["preset"].getBool();
	if (keyData["evodd_price"].defined()) {
		p.evodd_price = keyData["evodd_price"].getNumber();
	}
	if (!loaded) {
		saveSettings();
	}
	p.updateLiq(p.prev_price);
}

json::Value Interface::TestPair::collectSettings() const {
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
	  	  ("src_currency", src_currency)
		  ("activityCounter", activityCounter)
		  ("last_order_price", last_order_price)
		  ("loaded",true)
		  ("preset",preset)
		  ("evodd_price", evodd_price);
	return kv;
}


json::Value mergeAssets(Value options, const std::vector<std::string> &a) {
	Object o(options);
	std::string label;
	for (auto &&k : a){
		label.clear();
		std::transform(k.begin(), k.end(),std::back_inserter(label), toupper);
		o.set(k,label);
	}
	return o;
}

inline json::Value Interface::getSettings(const std::string_view & pair) const {
	const auto &pptr = getPairPtr(pair);
	if (pptr == nullptr) return json::undefined;
	const auto &p = *pptr;

	Value opt_src;

	if (p.preset) {

		opt_src = presetSettingsForm;

	} else {

		opt_src = settingsForm;
	}

	Value kv = p.collectSettings();
	CWSource::Pairs cwAssets = cwsource.getAssets();

	return opt_src.map([&](Value v) {
		StrViewA n = v["name"].getString();
		Value z = v.replace("default", kv[n]);
		if (z["name"].getString() == "src_asset") {
			z = z.replace("options", mergeAssets(z["options"], cwAssets.assets));
		}
		else if (z["name"].getString() == "src_currency") {
			z = z.replace("options", mergeAssets(z["options"], cwAssets.currencies));
		} else if (z["name"].getString() == "liq") {
			if (p.asset_balance > 0) {
				z = z.replace("default",p.inverted?1.0/p.low_liq:p.high_liq);
			} else if (p.asset_balance < 0) {
				z = z.replace("default",p.inverted?1.0/p.high_liq:p.low_liq);
			}
		} else if (z["name"].getString() == "bfx_src") {
			const auto &pairs = bfxsource.getPairs();
			z = z.replace("options",Value(json::object, pairs.begin(), pairs.end(), [](const auto &p){
				return Value(p.second.symbol, p.second.symbol);
			}));
		} else if (z["name"].getString() == "pair") {
			z = z.replace("default", pair);
		}
		return z;
	});
}

inline json::Value Interface::saveSettings() {
	json::Array r;
	for (auto &&k: pairs) r.push_back(k.second.collectSettings().replace("pairId", k.first));

	std::ofstream f(fname, std::ios::out|std::ios::trunc);
	Value s = r;
	s.toStream(f);
	return s;
}

inline void Interface::loadSettings() {
	std::ifstream f(fname);
	if (!f) return;
	Value v = Value::fromStream(f);
	if (!f) return;
	pairs.clear();
	if (v.type() == json::object) {
		std::hash<std::string_view> h;
		setSettings(v,true,h("TRAINER_PAIR"));
	} else {
		for (Value x: v) {
			setSettings(x,true,x["pairId"].getUInt());
		}
	}
}

extern const char *index_html;

static void interpolate(std::vector<double> &x, double b, double e, int cnt) {
	double mx(cnt);
	for (int i = 0; i < cnt; i++) {
		double f = i / mx;
		x.push_back(b + (e - b)*f);
	}
}

template<typename Iter>
static void normalizeBlock(Iter begin, Iter end) {
	double x = 0;
	double sx = 0;
	double sy = 0;
	double sxy = 0;
	double sxx = 0;
	double syy = 0;
	for (Iter c = begin; c != end; ++c) {
		double y = *c;
		sx = sx + x;
		sy = sy + y;
		sxy = sxy + x*y;
		sxx = sxx + x*x;
		syy = syy + y*y;
		x = x + 1;
	}
	double beta = (x*sxy - sx*sy)/(x*sxx - sx*sx);
	double alpha = (sy/x) - beta*(sx/x);

	x = 0;
	for (Iter c = begin; c != end; ++c) {
		double y = beta*x + alpha;
		double &s = *c;
		x = x + 1;
		s -= y;
	}
	double p = beta*(x/2) + alpha;
	for (Iter c = begin; c != end; ++c) {
		double &s = *c;
		s /= p;
	}


}

static void normalize(std::vector<double> &bk, unsigned int blockSize) {
	unsigned int size = bk.size();
	for (unsigned int i = 0; i < size; i+=blockSize) {
		auto b = bk.begin()+i;
		auto e = b + blockSize;
		normalizeBlock(b,e);
	}
}

static std::vector<double> generatePrices(StrViewA a, StrViewA c, unsigned int days) {
	Value p1 = cwsource.getPrices(a, c, days, 14400);
	std::vector<double> prc;
	double b = p1[0].getNumber();
	for (Value v : p1) {
		double e = v.getNumber();
		interpolate(prc, b, e, 14400/60);
		b = e;
	}
	Value p2 = cwsource.getPrices(a, c, 100, 180);
	std::vector<double> bk;
	b = p2[0].getNumber();
	for (Value v: p2) {
		double e = v.getNumber();
		interpolate(bk, b, e, 3);
		b = e;
	}
	{
		unsigned int remain = bk.size() % 144;
		if (remain) bk.erase(bk.begin(), bk.begin() + remain);
	}
	normalize(bk, 144);
	std::vector<double> res;
	double mult = 1;
	for (auto i1 = prc.begin(), i2 = bk.begin(); i1 != prc.end(); ++i1) {
		res.push_back(*i1 * (1 + *i2*mult));
		++i2;
		if (i2 == bk.end()) {
			i2 = bk.begin();
			mult = -mult;
		}
	}

	return res;
}

Interface::PageData Interface::fetchPage(const std::string_view &method, const std::string_view &vpath, const Interface::PageData &pageData) {

	if (method == "GET") {
		if (vpath == "/") {
			return Interface::PageData {200,{{"Content-Type","text/html;charset=utf-8"}},index_html};
		}
		if (vpath == "/symbols") {
			Object resp;
			resp.set("a", mergeAssets(json::object, cwsource.getAssets().assets));
			resp.set("c", mergeAssets(json::object, cwsource.getAssets().currencies));
			return Interface::PageData {200,{{"Content-Type","application/json"}},Value(resp).stringify().str()};
		}
		if (vpath.substr(0,8) == "/prices-") {
			StrViewA req (vpath.substr(8));
			auto splt = req.split("-");
			StrViewA a = splt();
			StrViewA c = splt();
			StrViewA d = splt();
			unsigned int days = Value(d).getUInt();

			auto prices = generatePrices(a, c, days);
			std::ostringstream s;
			for (auto &&p : prices) s << p << std::endl;
			return Interface::PageData {200,{{"Content-Type","application/octet-stream"}},s.str()};
		}
	}

	return Interface::PageData {404,{},""};
}

inline Interface::TestPair& Interface::getPair(const std::string_view &name) {
	std::hash<std::string_view> h;
	auto hsh = h(name);
	auto piter = pairs.find(hsh);
	if (piter == pairs.end()) {
		TestPair &p = pairs[hsh];
		p.init(name);
		return p;
	} else {
		piter->second.updateActivity();
		return piter->second;
	}
/*	TestPair &p = pairs[h(name)];
	p.activityTime = time(nullptr);
	p.activityCounter = 10;
	return p;*/
}

inline const Interface::TestPair* Interface::getPairPtr(const std::string_view &name) const {
	std::hash<std::string_view> h;
	auto idx = h(name);
	auto iter = pairs.find(idx);
	if (iter == pairs.end()) return nullptr;
	return &iter->second;
}

bool isTestPair(StrViewA pair) {
	return pair.startsWith("TEST") && pair.indexOf("/") == pair.npos;
}

void Interface::TestPair::init(const StrViewA &name) {
	updateActivity();
	preset = false;
	if (!isTestPair(name)) {
		const auto &p = bfxsource.getPairs();
		auto iter = std::find_if(p.begin(), p.end(), [&](const auto &pair) {
			return pair.second.symbol == name;
		});
		if (iter != p.end()) {
			double price = bfxsource.getPrice(iter->second.tsymbol.str());
			double suggest_fiat = iter->second.min_size * price * 10000;
			double l = std::floor(std::log10(suggest_fiat));
			this->currency_balance = std::pow(10, l);
			this->currency_step = std::pow(10,std::round(std::log10(price)-5));
			this->asset = iter->second.asset.str();
			this->currency = iter->second.currency.str();
			this->asset_balance = 0;
			this->asset_step = iter->second.min_size;
			this->futures = false;
			this->inverted = false;
			this->inited = true;
			this->price_source = "bitfinex";
			this->price_url = bfxsource.getPriceURL(iter->second.tsymbol.str());
			this->price_path = "[6]";
			this->preset = true;
		}
	}
}

void Interface::TestPair::updateActivity() {
	activityTime = time(nullptr);
	activityCounter = 10;
}

double BitfinexSource::getPrice(const std::string_view &tsymbol) {
	Value jprice = httpc.GET(getPriceURL(tsymbol));
	return jprice[6].getNumber();
}

std::string BitfinexSource::getPriceURL(const std::string_view &tsymbol) {
	std::string req("https://api.bitfinex.com/v2/ticker/");
	req.append(tsymbol);
	return req;

}

inline json::Value Interface::getMarkets() const {
	auto rs = const_cast<Interface *>(this)->getAllPairs();
	Object out;
	Object bfx;
	for (const auto &x:rs) {
		if (isTestPair(x)) {
			out.set("Blank", x);
		} else {
			auto splt = StrViewA(x).split("/");
			auto obj = bfx.object(splt());
			obj.set(splt(),x);
		}
	}
	out.set("Pre-made", bfx);
	return out;
}
