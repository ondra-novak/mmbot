/*
 * papertrading.cpp
 *
 *  Created on: 16. 1. 2022
 *      Author: ondra
 */


#include "papertrading.h"

#include <chrono>
#include <imtjson/binary.h>
#include "../imtjson/src/imtjson/object.h"

PaperTrading::PaperTrading(PStockApi s):source(s) {
	orderCounter = 1;
}

double PaperTrading::getBalance(const std::string_view &symb, const std::string_view &pair) {
	if (pairId != pair) return 0;

	auto iter = wallet.find(symb);
	double b;
	if (minfo.has_value() && minfo->leverage != 0 && collateral.has_value() && symb == minfo->currency_symbol) {
		b = collateral->getEquity(ticker.last);
	} else if (iter == wallet.end()) {
		b = source->getBalance(symb, pair);
		wallet.emplace(std::string(symb), b);
	} else {
		b = iter->second;
	}
	return b;
}


IStockApi::TradesSync PaperTrading::syncTrades(json::Value lastId,const std::string_view &pair) {
	if (pairId != pair) {
		return {};
	}
	if (needLoadWallet) {
		if (lastId.hasValue()) importState(lastId);
		needLoadWallet = false;
	}

	std::uint64_t from = lastId["l"].getUIntLong();

	auto itr = std::upper_bound(trades.begin(), trades.end(),  Trade{nullptr,from, 0,0,0,0}, [](const Trade &a, const Trade &b){
		return a.time < b.time;
	});
	TradeHistory out(itr, trades.end());


	return TradesSync{
		std::move(out), exportState()
	};
}

IStockApi::MarketInfo PaperTrading::getMarketInfo(const std::string_view &pair) {
	if (pairId != pair) {
		openOrders.clear();
		trades.clear();
		collateral.reset();
		minfo.reset();
		pairId = pair;

	}
	if (!minfo.has_value()) {
		minfo = source->getMarketInfo(pair);
		minfo->simulator=true;
		ticker = source->getTicker(pair);
		minfo->wallet_id.append("(paper_trading)");
	}

	return *minfo;
}

//Trading function is implement in reset()
//it loads ticker and performs simulation of market matching
bool PaperTrading::reset() {

	//we need minfo loaded
	if (minfo.has_value()) {

		//get ticker
		ticker = source->getTicker(pairId);

		//if walllet loaded and leveraged market but collateral is not initialized
		if (!needLoadWallet && minfo->leverage && !collateral.has_value()) {
			//so initialize now from wallet
			//we need ticker to initialize collateral
			collateral.emplace(ticker.last, getBalance(minfo->asset_symbol, pairId), getBalance(minfo->currency_symbol, pairId));
		}

		TradeHistory trades;
		int cnt = 0;
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		//each trade has id <time>-<counter>
		auto itr = std::remove_if(openOrders.begin(), openOrders.end(),[&](const Order &ord){

			cnt++;
			//matching condition
			//orders below ticker for sell and orders above ticker for buys are matched
			if ((ord.price - ticker.last)*ord.size >= 0) {
				//partial execution is not simulated

				char tradeID[100];
				snprintf(tradeID,100,"%lX-%X", now, cnt);
				//create trade from order
				Trade t;
				t.id = tradeID;
				t.time = static_cast<uint64_t>(now) * 1000;
				t.price = ord.price;
				t.size = ord.size;
				t.eff_price = t.price;
				t.eff_size = t.size;
				//simulate fees
				minfo->removeFees(t.eff_size, t.eff_price);
				//push to list
				trades.push_back(t);
				return true;

			} else {
				return false;
			}

		});
		//remove executed orders
		openOrders.erase(itr, openOrders.end());

		//some trades?
		if (!trades.empty()) {

			//sort them by increasing distance from ticker
			std::sort(trades.begin(), trades.end(),[&](const Trade &a, const Trade &b){
				double da = std::abs(a.price - ticker.last);
				double db = std::abs(b.price - ticker.last);
				return da < db;
			});

			//process all trades and update balances
			for (const Trade &t: trades) {
				//if leveraged market
				if (minfo->leverage) {
					//update collateral
					(*collateral)(t.eff_price, t.eff_size);
					//update wallet
					wallet[minfo->currency_symbol] = collateral->getEquity(t.price);
					wallet[minfo->asset_symbol] = collateral->getPos();
				} else {
					//update wallet for spot trade
					wallet[minfo->asset_symbol] = getBalance(minfo->asset_symbol, pairId) + t.eff_size;
					wallet[minfo->currency_symbol] = getBalance(minfo->currency_symbol, pairId) - t.eff_size*t.eff_price;
				}
			}
			//append to list of trades for syncTrades
			std::copy(trades.begin(), trades.end(), std::back_inserter(this->trades));
		}
	}

	return true;
}


IStockApi::Orders PaperTrading::getOpenOrders(const std::string_view &pair) {
	if (pairId != pair) return {};
	return openOrders;
}

json::Value PaperTrading::placeOrder(const std::string_view &pair, double size,
		double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {

	if (pair != pairId) {
		throw std::runtime_error("Market is closed");
	}

	if (replaceId.hasValue()) {
		auto iter = std::remove_if(openOrders.begin(), openOrders.end(), [&](const Order &o){
			return o.id == replaceId;
		});
		openOrders.erase(iter, openOrders.end());
	}
	if (size) {
		openOrders.push_back({
			++orderCounter,
			clientId,
			size,
			price
		});
		return orderCounter;
	} else {
		return nullptr;
	}

}

double PaperTrading::getFees(const std::string_view &pair) {
	if (!minfo.has_value()) {
		getMarketInfo(pair);
	}
	return minfo->fees;
}

IStockApi::Ticker PaperTrading::getTicker(const std::string_view &pair) {
	if (pairId != pair) return source->getTicker(pair);
	else return ticker;
}

json::Value PaperTrading::exportState() {
	json::Object obj;
	obj.set("w",json::Value(json::object,wallet.begin(), wallet.end(), [](const auto &x){
				return json::Value(x.first,x.second);
			}));
	if (collateral.has_value()) {
		obj.set("c", {
			collateral->getPos(),
			collateral->getRPnL(),
			collateral->getOpen()
		});
	}
	if (!trades.empty()) {
		obj.set("l", trades.back().time);
	}
	return obj;
}

void PaperTrading::importState(json::Value v) {
	for (json::Value item:v["w"]) {
		wallet[item.getKey()]= item.getNumber();
	}
	auto c = v["c"];
	if (c.defined()) {
		double pos,rpnl, open;
		pos = c[0].getNumber();
		rpnl = c[1].getNumber();
		open = c[2].getNumber();
		collateral.emplace(open, pos, rpnl);
	}
}

json::Value PaperTrading::getMarkets() const {
	return json::Object();
}

std::vector<std::string> PaperTrading::getAllPairs() {
	return {};
}

void PaperTrading::restoreSettings(json::Value v) {
}

json::Value PaperTrading::setSettings(json::Value v) {
	for (json::Value witem: v) {
		wallet[witem.getKey()] = witem.getNumber();
	}
	if (collateral.has_value()) {
		double eq = collateral->getEquity(ticker.last);
		double diff = getBalance(minfo->currency_symbol, "") - eq;
		collateral = ACB(collateral->getOpen(), collateral->getPos(), collateral->getRPnL()+diff);
	}
	return json::undefined;
}

IBrokerControl::PageData PaperTrading::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const IBrokerControl::PageData &pageData) {
	return {};
}

json::Value PaperTrading::getSettings(const std::string_view &pairHint) const {
	return json::Value(json::array, wallet.begin(), wallet.end(),[](const Wallet::value_type &witem){
		return json::Object {
			{"name", witem.first},
			{"label", witem.first},
			{"type","number"},
			{"default", witem.second}
		};
	});
}

IBrokerControl::BrokerInfo PaperTrading::getBrokerInfo() {
	std::string img;
	auto ptr = dynamic_cast<IBrokerControl *>(source.get());
	if (ptr) img = ptr->getBrokerInfo().favicon;
	else img = json::base64->decodeBinaryValue(
			"iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAMAAABg3Am1AAAAM1BMVEVAAAAAAgAXGRYnKSc5OzhG"
			"SEVYWldmaGV2eHWJjImXmZapq6i3urbKzcna3dnq7Ony9fHspJQpAAAAAXRSTlMAQObYZgAAArNJ"
			"REFUSMetVotypDAMq0PIm8T//7UnOcDClm3vZs7D7NLWii3Ldvr19V9N5F98zT796cHZ59Y1yfdw"
			"IqVGuR+c6jbMqtxPsZdSS/Fy/CqU1sdpXb6nKBWAzB/ceTBd59cjoMAkbUN1XM6eCH/3N0BGgCz1"
			"5g7/Cciy+4cXh1gBWOLLv+RXhLYDQqvrARBGSKJn7lXeWYvU1lo46kpAln4C+nzvBR+6Z9Fqq6cQ"
			"2QDtzGkTV+yLAEo3A/ijYhIZwmU9s5DFkhIUWSmdpIban/0gCyMEf0ZAMPo2kagKEuIqzF1Er/nC"
			"eoxYRqzAwYbyq8A/X7tqst4ORI1akuoC1011lRUJ1WuDSsrZpNs7YhPFo2wXyBnAuLZr2+6sl0RA"
			"pGgoK5JpbFtAIwC3AJQOEaKRKB4t4skadGHIiIzDfWIMAOmUpal9ZMveWBfJt5K+sUYCIhCkMHmN"
			"1vMM4N9H0li7QsDYFviueNciZ0nfAZ4hwor0UR3w1RJ0WLhQ30r6dWtYFH9Tx9Ss21nXN83uJDqK"
			"X/lMAdWhpE8BrlPXorZ0SO4sQHoCmHQ+sdf4HDsmzwAPAIcxLUYClPdZ6sYAc7Y97D9bHZn1iU3D"
			"nKWNCw2aZX0C0L9QuhKbzFlqkFmyQyniAyARMKWTvdHR5SNZw9YHEoE5rQEketiHNbkOeBo85GGD"
			"E2CstboJWLm9MHnU/yMJSDd6mADBGHPXsnQPgMwQGH+zvarB6qscvgfWXOI+vzZslzK9ETM+ADwS"
			"QPdfVnKnc29lfb65OMCBe2WmxI+NbSSfLkbrg0Tp5sGZW8bFlD9dpMiJfVB5sM2mj9y5KMQnQCDr"
			"IGSyxGQDAivXFfk9pZKXkIpJwocv8ePlbpfj1XJcfuA8r1+D8FKNXn50PlnDOUX3q++OWHP6i4Pf"
			"/8X4zekPc88zkXyAgKsAAAAASUVORK5CYII="
	).getString();

	return BrokerInfo{
		true,
		"paper_trading",
		"simulator",
		"",
		"1.0.0",
		"",
		img,
		true,
		false
	};
}

IBrokerControl::AllWallets PaperTrading::getWallet() {
	AllWallets aw;
	IBrokerControl::Wallet w1;
	w1.walletId="spot";
	for (const auto &w: wallet) {
		w1.wallet.push_back(WalletItem{
			w.first, w.second
		});
	}
	aw.push_back(w1);
	if (collateral.has_value()) {
		IBrokerControl::Wallet w2;
		w2.walletId="leveraged";
		w2.wallet.push_back({"position",collateral->getPos()});
		w2.wallet.push_back({"equity",collateral->getEquity(ticker.last)});
		aw.push_back(w2);
	}
	return aw;

}

std::string PaperTrading::getIconName() const {
	auto ptr = dynamic_cast<IBrokerIcon *>(source.get());
	if (ptr) return ptr->getIconName();
	else return "";
}

void PaperTrading::saveIconToDisk(const std::string &path) const {
	auto ptr = dynamic_cast<IBrokerIcon *>(source.get());
	if (ptr) return ptr->saveIconToDisk(path);
}
