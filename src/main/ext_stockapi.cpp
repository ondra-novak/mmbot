/*
 * ext_stockapi.cpp
 *
 *  Created on: 21. 5. 2019
 *      Author: ondra
 */




#include "ext_stockapi.h"

#include <imtjson/object.h>
#include <imtjson/binary.h>
#include <fstream>
#include <set>

#include "../shared/finally.h"
using namespace ondra_shared;



ExtStockApi::ExtStockApi(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline, int timeout)
:connection(std::make_shared<Connection>(workingDir, name, cmdline, timeout)) {
}



double ExtStockApi::getBalance(const std::string_view & symb, const std::string_view & pair) {
	return requestExchange("getBalance",
			json::Object({{"pair", pair},
						{"symbol", symb}})).getNumber();

}


ExtStockApi::TradesSync ExtStockApi::syncTrades(json::Value lastId, const std::string_view & pair) {
	auto r = requestExchange("syncTrades",json::Object({{"lastId",lastId},
														{"pair",pair}}));
	TradeHistory  th;
	for (json::Value v: r["trades"]) th.push_back(Trade::fromJSON(v));
	return TradesSync {
		th, r["lastId"]
	};
}

ExtStockApi::Orders ExtStockApi::getOpenOrders(const std::string_view & pair) {
	Orders r;

	auto v = requestExchange("getOpenOrders",pair);
	for (json::Value x: v) {
		Order ord {
			x["id"],
			x["clientOrderId"],
			x["size"].getNumber(),
			x["price"].getNumber()
		};
		r.push_back(ord);
	}
	return r;
}

ExtStockApi::Ticker ExtStockApi::getTicker(const std::string_view & pair) {
	auto resp =  requestExchange("getTicker", pair);
	return Ticker {
		resp["bid"].getNumber(),
		resp["ask"].getNumber(),
		resp["last"].getNumber(),
		resp["timestamp"].getUIntLong(),
	};
}

json::Value  ExtStockApi::placeOrder(const std::string_view & pair,
		double size, double price,json::Value clientId,
		json::Value replaceId,double replaceSize) {

	return requestExchange("placeOrder",json::Object({
		{"pair",pair},
		{"price",price},
		{"size",size},
		{"clientOrderId",clientId},
		{"replaceOrderId",replaceId},
		{"replaceOrderSize",replaceSize}}));
}


bool ExtStockApi::reset() {
	connection->housekeeping(5);
	std::unique_lock _(connection->getLock());
	//save housekeep counter to avoid reset treat as action
	if (connection->isActive()) try {
		requestExchange("reset",json::Value(),true);
	} catch (...) {
		requestExchange("reset",json::Value(),true);
	}
	return true;
}

ExtStockApi::MarketInfo ExtStockApi::getMarketInfo(const std::string_view & pair) {
	json::Value v = requestExchange("getInfo",pair);

	MarketInfo res;
	res.asset_step = v["asset_step"].getNumber();
	res.currency_step = v["currency_step"].getNumber();
	res.asset_symbol = v["asset_symbol"].getString();
	res.currency_symbol = v["currency_symbol"].getString();
	res.min_size = v["min_size"].getNumber();
	res.min_volume= v["min_volume"].getNumber();
	res.fees = v["fees"].getNumber();
	res.feeScheme = strFeeScheme[v["feeScheme"].getString()];
	res.leverage= v["leverage"].getNumber();
	res.invert_price= v["invert_price"].getBool();
	res.simulator= v["simulator"].getBool();
	res.inverted_symbol= v["inverted_symbol"].getString();
	res.private_chart = v["private_chart"].getBool();
	res.wallet_id = v["wallet_id"].getString();
	return res;

}

double ExtStockApi::getFees(const std::string_view& pair) {
	json::Value v = requestExchange("getFees",pair);
	return v.getNumber();

}

std::vector<std::string> ExtStockApi::getAllPairs() {
	json::Value v = requestExchange("getAllPairs", json::Value());
	std::vector<std::string> res;
	res.reserve(v.size());
	for (json::Value x: v) res.push_back(std::string(x.toString().str()));
	return res;
}

void ExtStockApi::Connection::onConnect() {
	binary_mode = false;
	try {
		jsonRequestExchange("bin", json::Value());
		binary_mode = true;
	} catch (...) {
		//empty
	}
	ondra_shared::LogObject lg("");
	bool debug= lg.isLogLevelEnabled(ondra_shared::LogLevel::debug);
	if (debug) {
		try {
			jsonRequestExchange("enableDebug",debug, false);
		} catch (AbstractExtern::Exception &) {

		}
	}
	broker_info = jsonRequestExchange("getBrokerInfo", json::Value());
	instance_counter++;
}

json::Value ExtStockApi::Connection::getBrokerInfo() const {
	return broker_info;
}
ExtStockApi::BrokerInfo ExtStockApi::getBrokerInfo()  {

	try {
		auto resp = connection->getBrokerInfo();
		if (!resp.defined()) {
			connection->preload();
			resp = connection->getBrokerInfo();
		}
		/*auto resp = requestExchange("getBrokerInfo", json::Value());*/
		std::string name = connection->getName();
		if (!subaccount.empty()) name = name + "~" + subaccount;
		return BrokerInfo {
			resp["trading_enabled"].getBool(),
					name,
			resp["name"].getString(),
			resp["url"].getString(),
			resp["version"].getString(),
			resp["licence"].getString(),
			json::map_bin2str(resp["favicon"].getBinary()),
			resp["settings"].getBool(),
			subaccount.empty()?resp["subaccounts"].getBool():false
		};
	} catch (AbstractExtern::Exception &) {
		return BrokerInfo {
			true,
			connection->getName(),
			connection->getName(),
		};
	}

}

void ExtStockApi::setApiKey(json::Value keyData) {
	requestExchange("setApiKey",keyData);
}

json::Value ExtStockApi::getApiKeyFields() const {
	return const_cast<ExtStockApi *>(this)->requestExchange("getApiKeyFields",json::Value());
}

json::Value ExtStockApi::getSettings(const std::string_view & pairHint) const {
	return const_cast<ExtStockApi *>(this)->requestExchange("getSettings",json::Value(pairHint));
}

json::Value ExtStockApi::setSettings(json::Value v) {
	return (broker_config = requestExchange("setSettings", v));
}

void ExtStockApi::restoreSettings(json::Value v) {
	broker_config = v;
	if (connection->isActive()) {
		requestExchange("restoreSettings", v);
	}
}


class IconFiles: public std::set<std::string> {
public:
    ~IconFiles() {
        for (auto &&k: *this) {
            std::remove(k.c_str());
        }
    }
};


static IconFiles iconFiles;

void ExtStockApi::saveIconToDisk(const std::string &path) const {
        std::unique_lock _(connection->getLock());


        std::string name =getIconName();
        std::string fullpath = path+"/"+name;
        if (iconFiles.find(fullpath) == iconFiles.end()) {
                std::ofstream f(fullpath, std::ios::out|std::ios::trunc|std::ios::binary);
                BrokerInfo binfo = const_cast<ExtStockApi *>(this)->getBrokerInfo();
                f.write(reinterpret_cast<const char *>(binfo.favicon.data()), binfo.favicon.size());
                if (!f) {
                    logError("Failed to create icon: $1", fullpath);
                } else {
                    iconFiles.insert(fullpath);
                    ondra_shared::logProgress("Created icon: $1", fullpath);
                }
        }
}

std::string ExtStockApi::getIconName() const {
	return "fav_"+connection->getName()+".png";
}

ExtStockApi::PageData ExtStockApi::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const PageData &pageData) {
	json::Value v = json::Object({{"method", method},
		{"path", vpath},
		{"body", pageData.body},
		{"headers", json::Value(json::object, pageData.headers.begin(), pageData.headers.end(),[](auto &&p) {
					return json::Value(p.first, p.second);
			})}});

	json::Value resp = requestExchange("fetchPage", v);
	PageData presp;
	presp.body = resp["body"].toString().str();
	presp.code = resp["code"].getUInt();
	for (json::Value v: resp["headers"]) {
		presp.headers.emplace_back(v.getKey(), v.toString().str());
	}
	return presp;
}

json::Value ExtStockApi::requestExchange(json::String name, json::Value args, bool idle) {
	if (connection->wasRestarted(instance_counter)) {
		if (broker_config.defined()) {
			if (subaccount.empty()) connection->jsonRequestExchange("restoreSettings", broker_config, idle);
			else connection->jsonRequestExchange("subaccount", {subaccount, "restoreSettings", broker_config}, idle);
		}
	}
	if (subaccount.empty()) return connection->jsonRequestExchange(name, args, idle);
	else try {
		return connection->jsonRequestExchange("subaccount", {subaccount, name, args}, idle);
	} catch (const AbstractExtern::Exception &e) {
		throw AbstractExtern::Exception(std::string(e.getMsg()), connection->getName()+ "~" + subaccount, name.c_str());
	} catch (std::exception &e) {
		throw AbstractExtern::Exception(e.what(), connection->getName() + "~" + subaccount, name.c_str());
	}
}

void ExtStockApi::stop() {
	connection->stop();
}

bool ExtStockApi::Connection::wasRestarted(int& counter) {
	preload();
	int z = instance_counter;
	if (z != counter) {
		counter = z;
		return true;
	} else {
		return false;
	}
}

bool ExtStockApi::isSubaccount() const {
	return !subaccount.empty();
}

json::Value ExtStockApi::getMarkets() const {
	return const_cast<ExtStockApi *>(this)->requestExchange("getMarkets",json::Value());

}

ExtStockApi::AllWallets ExtStockApi::getWallet()  {
	AllWallets w;
	auto resp = requestExchange("getWallet",json::Value());
	for (json::Value x: resp) {
		Wallet sw;
		for (json::Value y: x) {
			if (y.getNumber()) {
				sw.wallet.push_back({
					y.getKey(), y.getNumber()
				});
			}
		}
		sw.walletId = x.getKey();
		w.push_back(sw);
	}
	return w;
}

bool ExtStockApi::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	try {
		auto resp = requestExchange("areMinuteDataAvailable", {asset, currency});
		return resp[1].getBool();
	} catch (...) {
		return false;
	}
}

std::uint64_t ExtStockApi::downloadMinuteData(const std::string_view &asset,
		const std::string_view &currency, const std::string_view &hint_pair,
		std::uint64_t time_from, std::uint64_t time_to,
		std::vector<OHLC> &data) {
	try {
		auto resp = requestExchange("downloadMinuteData", json::Object{
			{"asset",asset},
			{"currency",currency},
			{"hint_pair",hint_pair},
			{"time_from",time_from},
			{"time_to",time_to},
		});
		json::Value recv_data = resp["data"];
		json::Value start_time = resp["start"];
		data.clear();
		data.reserve(recv_data.size());
		double last = 0;
		for (json::Value v: recv_data) {
			if (v.type() == json::number) {
				double d = v.getNumber();
				last = d;
				recv_data.push({d,d,d,d});
			} else if (v.type() == json::array) {
				double o=last,h,l,c;
				switch (v.size()) {
				case 1: h=l=c = v[0].getNumber();break;
				case 2: h=v[0].getNumber();l =v[1].getNumber();c=std::sqrt(h*l);break;
				case 3: h=v[0].getNumber();l =v[1].getNumber();c=v[2].getNumber();break;
				case 4: o=v[0].getNumber();h =v[1].getNumber();l=v[2].getNumber();c=v[3].getNumber();break;
				}
				if (o == 0) o = std::sqrt(h*l);
				h = std::max({o,h,l,c});
				l = std::min({o,h,l,c});
				last = c;
				recv_data.push({o,h,l,c});
			}
		}
		return start_time.getUIntLong();
	} catch (...) {
		return 0;
	}

}

ExtStockApi::ExtStockApi(std::shared_ptr<Connection> connection, const std::string &subaccount)
	:connection(connection),subaccount(subaccount) {}


ExtStockApi *ExtStockApi::createSubaccount(const std::string &subaccount) const  {
	ExtStockApi *copy = new ExtStockApi(connection,subaccount);
	return copy;
}
