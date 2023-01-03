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

#include "../shared/trailer.h"
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


void ExtStockApi::reset(const std::chrono::system_clock::time_point &tp) {
	std::unique_lock _(connection->getLock());

	if (lastReset < tp) {
		if (connection->isActive()) try {
			requestExchange("reset",json::Value());
		} catch (...) {
			requestExchange("reset",json::Value());
		}
		lastReset = tp;
	}
}

ExtStockApi::MarketInfo ExtStockApi::getMarketInfo(const std::string_view & pair) {
	json::Value v = requestExchange("getInfo",pair);

	return MarketInfo::fromJSON(v);

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
			jsonRequestExchange("enableDebug",debug);
		} catch (AbstractExtern::Exception &) {

		}
	}
	broker_info = jsonRequestExchange("getBrokerInfo", json::Value());
	instance_counter++;
}

json::Value ExtStockApi::Connection::getBrokerInfo() const {
	return broker_info;
}

void ExtStockApi::Connection::refreshBrokerInfo() {
	broker_info = jsonRequestExchange("getBrokerInfo", json::Value());
}
ExtStockApi::BrokerInfo ExtStockApi::getBrokerInfo()  {

	try {
		json::Value resp;
		if (subaccount.empty()) {
			resp = connection->getBrokerInfo();
			if (!resp.defined()) {
				connection->preload();
				resp = connection->getBrokerInfo();
			}
		} else {
			resp = requestExchange("getBrokerInfo", json::Value());
		}
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
			subaccount.empty()?resp["subaccounts"].getBool():false,
			resp["nokeys"].getBool(),
			resp["datasrc"].getBool(),

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
	connection->refreshBrokerInfo();
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

json::Value ExtStockApi::requestExchange(json::String name, json::Value args) {
	lastActivity = std::chrono::system_clock::now();
	if (connection->wasRestarted(instance_counter)) {
		if (broker_config.defined()) {
			if (subaccount.empty()) connection->jsonRequestExchange("restoreSettings", broker_config);
			else connection->jsonRequestExchange("subaccount", {subaccount, "restoreSettings", broker_config});
		}
	}
	if (subaccount.empty()) return connection->jsonRequestExchange(name, args);
	else try {
		return connection->jsonRequestExchange("subaccount", {subaccount, name, args});
	} catch (const AbstractExtern::Exception &e) {
		throw;
	} catch (std::exception &e) {
		throw AbstractExtern::Exception(e.what(), connection->getName() + "~" + subaccount, name.c_str(), false);
	}
}

void ExtStockApi::unload() {
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
		return resp.getBool();
	} catch (...) {
		return false;
	}
}

std::uint64_t ExtStockApi::downloadMinuteData(const std::string_view &asset,
		const std::string_view &currency, const std::string_view &hint_pair,
		std::uint64_t time_from, std::uint64_t time_to,
		HistData &xdata) {

	auto resp = requestExchange("downloadMinuteData", json::Object{
			{"asset",asset},
			{"currency",currency},
			{"hint_pair",hint_pair},
			{"time_from",time_from},
			{"time_to",time_to},
		});
    json::Value recv_data = resp["data"];
    json::Value start_time = resp["start"];
    if (recv_data.empty() || !recv_data[0].isContainer())  {
        MinuteData data;
        for (json::Value v: recv_data) {
            data.push_back(v.getNumber());
        }
        xdata = std::move(data);
    } else {
        OHLCData data;
        for (json::Value v: recv_data) {
            data.push_back({
                v[0].getNumber(),
                v[1].getNumber(),
                v[2].getNumber(),
                v[3].getNumber(),
            });
        }
        
        xdata = std::move(data);
    }
    return start_time.getUIntLong();


}

bool ExtStockApi::isIdle(const std::chrono::system_clock::time_point &tp) const {
	auto idleTime = tp - (subaccount.empty()? connection->getLastActivity():lastActivity);
	return std::chrono::duration_cast<std::chrono::minutes>(idleTime).count() >= 15;
}

ExtStockApi::ExtStockApi(std::shared_ptr<Connection> connection, const std::string &subaccount)
	:connection(connection),subaccount(subaccount) {}


ExtStockApi *ExtStockApi::createSubaccount(const std::string &subaccount) const  {
	ExtStockApi *copy = new ExtStockApi(connection,subaccount);
	return copy;
}

std::chrono::system_clock::time_point ExtStockApi::Connection::getLastActivity() {
	return lastActivity;
}

json::Value ExtStockApi::Connection::jsonRequestExchange(json::String name, json::Value args) {
	lastActivity = std::chrono::system_clock::now();
	return AbstractExtern::jsonRequestExchange(name, args);

}
