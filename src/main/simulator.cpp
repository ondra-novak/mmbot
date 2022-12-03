#include "acb.h"
#include "istockapi.h"
#include "simulator_initcurrency.h"

#include "simulator.h"

#include <imtjson/array.h>
#include <imtjson/binary.h>
#include <imtjson/ivalue.h>
#include <imtjson/object.h>
#include <imtjson/operations.h>
#include <imtjson/string.h>
#include <imtjson/value.h>
#include <shared/countdown.h>
#include <shared/linear_map.h>
#include <shared/worker.h>

#include <algorithm>
#include <bits/stdint-uintn.h>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static inline double getInitialBalance(const std::string_view &symb) {
	auto iter = std::lower_bound(std::begin(initialBalance), std::end(initialBalance), std::pair{symb,0.0});
	if (iter != std::end(initialBalance) && iter->first == symb) return iter->second;
	else return 0;
}


Simulator::Simulator(IStockSelector *exchanges, const std::string &sub)
:exchanges(exchanges), sub(sub),lastReset(std::chrono::system_clock::now()) {}


bool Simulator::isSubaccount() const {
	return !sub.empty();
}
PStockApi Simulator::findSuitableHistoryBroker(const std::string_view &asset, const std::string_view &currency) {
	PStockApi src;
	exchanges->forEachStock([&](std::string_view id, const PStockApi &s){

		IBrokerControl *b = dynamic_cast<IBrokerControl *>(s.get());
		if (b) {
			auto bi = b->getBrokerInfo();
			if (bi.datasrc) {
				IHistoryDataSource *h = dynamic_cast<IHistoryDataSource *>(s.get());
				if (h->areMinuteDataAvailable(asset, currency)) {
					src = s;
				}
			}
		}
	});
	return src;
}

bool Simulator::areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) {
	std::lock_guard _(lock);
	return findSuitableHistoryBroker(asset, currency) != nullptr;

}

std::vector<std::string> Simulator::getAllPairs() {
	std::lock_guard _(lock);

	if (!allPairs.defined()) getMarkets();
	std::vector<std::string> ret;
	ret.reserve(allPairs.size());
	for (json::Value v: allPairs) ret.push_back(v.getString());
	return ret;

}

IStockApi* Simulator::createSubaccount(const std::string &name) const {
	return new Simulator(exchanges, name);
}

IBrokerControl::PageData Simulator::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const IBrokerControl::PageData &pageData) {
	return {};
}

json::Value Simulator::getSettings(const std::string_view &pairHint) const {
	std::lock_guard _(lock);

	json::Array out;

	auto cf_iter = _custom_fees.find(pairHint);

	for (int i = 0; i < 2; i++) {
		const Wallet &w = wallet[i];
		out.push_back(json::Object{
			{"label",i?"Futures wallet":"Spot wallet"},
			{"type","label"}
		});
		for (const auto &x: w) {
			out.push_back(json::Object{
				{"name",json::String({i?"f":"s",x.first})},
				{"label",x.first},
				{"type","number"},
				{"default",x.second.first}
			});
		}
	}
	out.push_back(json::Object{
	    {"label","Fee control"},
	    {"type","label"}
	});
    out.push_back(json::Object{
        {"label","Fee control"},
        {"type","enum"},
        {"name","_feec"},
        {"options",json::Object{
            {"default","Standard fee"},
            {"custom","Custom fee"},
        }},
        {"default",cf_iter == _custom_fees.end()?"default":"custom"}
        
    });
    out.push_back(json::Object{
        {"label","Custom fee [%]"},
        {"type","number"},
        {"name","_customfee"},
        {"showif",json::Object{
            {"_feec","custom"},            
        }},
        {"default",cf_iter == _custom_fees.end()?0.0:cf_iter->second}        
    });
    out.push_back(json::Object{
        {"label","pair"},
        {"type","string"},
        {"name","_pair"},
        {"showif",json::object},
        {"default",pairHint}        
    });
	return out;
}

uint64_t Simulator::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<IHistoryDataSource::OHLC> &data) {
	std::lock_guard _(lock);
	SourceInfo p = parseSymbol(hint_pair);
	IHistoryDataSource *src = dynamic_cast<IHistoryDataSource *>(p.exchange.get());
	if (src && src->areMinuteDataAvailable(asset, currency)) {
		return src->downloadMinuteData(asset, currency, hint_pair, time_from, time_to, data);
	} else {
		PStockApi s = findSuitableHistoryBroker(asset, currency);
		IHistoryDataSource *src = dynamic_cast<IHistoryDataSource *>(s.get());
		return src->downloadMinuteData(asset, currency, hint_pair, time_from, time_to, data);
	}
}

IBrokerControl::BrokerInfo Simulator::getBrokerInfo() {
	std::lock_guard _(lock);

	static json::Value bin = json::base64->decodeBinaryValue("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAM1BMVEVAAAB2en17fXqJi4iVmJSf"
			"op6mqKWytLG5u7fExsDP0MjZ2tHj49vs7OP09Ov9//z///aDjkt0AAAAAXRSTlMAQObYZgAAAgNJ"
			"REFUWMPtlsu2gyAMRSsobxL+/2tvAFGwSLUd3bXMoANJdkNyCLxejz322DULAUO0b6NDAax2P/zN"
			"biXvoQ66h6DMQfuD/w0CuXkFb86XCeTkRM/1OsBx6HpeA1D+kzmPHxOyh2CeythbNAsMCFu3Z4Y9"
			"txDsxESvtniQnGTQLSEwPnN7WEl6pWh01irBTSI42IBYOS5L3EX1LWMdfQWrFpaMS9uRMEZPw+Fd"
			"07FiLMeA0yIxJqGNe4d47jqHIsCUghadV51ZppIKYXwN0DHwQKAItpkw2d/phe8flYXmj48AzSqb"
			"uNDZ32+ZxGQ05D7tzcbt2DWATJlVSsWL6qOEswwE61r60wbuurOFesvOzNaLs9q7S5PmdQVABLdu"
			"qa4j6bkC4AhAguakkT0YbKyNYbAfFcQRwAapt2AjedpWsLkcVwA6yHQoSOjzVOoSfPp9XQHIDCjN"
			"mPJ2CIA7YFgDEdJoVamvWxs8M1jpQH4GSFJFUUEsH5e1kMxngINWQlztp5EKxEcAWSYLQA1AbOfc"
			"eReUyydLyCoDITA0l8g8AHBjFGfrpCsjsxmLSZziVAd5K20RsDNWwfTa6UIs0OLK8TsBrAtezz1A"
			"CQ/jm6lMvbYlq2rDdk0Nb7bMcKoah2vz8Pq7BHM9RIJw+OqBtW7GKgNfv8921f/0Qvwp/LHH/pX9"
			"AeUtPJnXDbhLAAAAAElFTkSuQmCC");

	std::string name = "simulator";
	if (!sub.empty()) name.append("~").append(sub);

	return {
		true,
		name,
		"Simulator",
		"",
		"1.0.1",
		"",
		json::map_bin2str(bin.getBinary(json::base64)),
		true,
		sub.empty(),
		true,
		false
	};

}

static json::Value translateIDS(const std::string_view &prefix, json::Value tree) {
	if (tree.isContainer()) {
		return tree.map([&](json::Value v){
			return translateIDS(prefix,v);
		});
	} else if (tree.type() == json::string) {
		return json::Value(tree.getKey(), json::String({prefix,":",tree.getString()}));
	} else {
		return tree;
	}
}

json::Value Simulator::getMarkets() const {
	std::lock_guard _(lock);
	json::Object out;
	ondra_shared::Worker wrk = ondra_shared::Worker::create(4);
	ondra_shared::Countdown cnt(0);
	std::mutex lk;
	exchanges->forEachStock([&](std::string_view id, const PStockApi &s){
		if (s.get() != this) {
			IBrokerControl *b = dynamic_cast<IBrokerControl *>(s.get());
			if (b) {
				cnt++;
				wrk >> [id, s = PStockApi(s), b, &lk, &out, &cnt]() mutable {
					try {
						auto bi = b->getBrokerInfo();
						if (bi.subaccounts) {
							auto sb = dynamic_cast<IBrokerSubaccounts *>(s.get());
							s = PStockApi(sb->createSubaccount("__simulator__"));
							IBrokerControl *q = dynamic_cast<IBrokerControl *>(s.get());
							if (q) b = q;
						}

						try {
							if (bi.datasrc) {
								json::Value x = translateIDS(id, b->getMarkets());
								std::lock_guard _(lk);
								out.set(bi.exchangeName,x);
							}
						} catch (std::exception &e) {
							std::lock_guard _(lk);
							out.set(bi.exchangeName,json::Object({
								{"ERROR",json::Object{
									{e.what(), json::object}}}
								}
							));
						}
					} catch (...) {

					}
					--cnt;
				};
			}
		}
	});
	cnt.wait();

	json::Array all;
	json::Value ret = out;
	ret.walk([&](json::Value z){
		if (z.type() == json::string) all.push_back(z.stripKey());
		return true;
	});
	allPairs = all;

	return ret;
}

double Simulator::getBalance(const std::string_view &symb, const std::string_view &pair) {
	std::lock_guard _(lock);
	AbstractPaperTrading::TradeState &st = getState(pair);
	if (st.minfo.leverage) {
		if (st.minfo.asset_symbol == symb) {
			return st.collateral.getPos();
		} else if (st.minfo.currency_symbol == symb) {
			return getRawBalance(st).currency + st.collateral.getUPnL(st.ticker.last);
		}
	} else {
		Wallet &w = wallet[chooseWallet(st.minfo)];
		auto itr = w.find(symb);
		if (itr == w.end()) return 0.0;
		else return itr->second.first;
	}
	return 0;
}

json::Value Simulator::setSettings(json::Value v) {
	std::lock_guard _(lock);
	
	for (json::Value x: v) {
		double val = x.getNumber();
		auto n = x.getKey();
		if (n.empty()) continue;
		char c = n[0];
		auto symb = n.substr(1);
		if (c != '_') {
            int wid = c=='f'?wallet_futures:wallet_spot;
            Wallet &w = wallet[wid];
            w[symb] = {val,true};
		}
	}
	
	std::string pair = v["_pair"].getString();
	bool custom_fee = v["_feec"].getString() == "custom";
	double fee = v["_customfee"].getNumber();
	
	auto siter = state.find(pair);
	if (custom_fee) {
	    _custom_fees[pair] = fee;
	    if (siter == state.end()) {
	        siter->second.fee_override = fee;
	    }
	}
	else {
	    _custom_fees.erase(pair);
        if (siter == state.end()) {
            siter->second.fee_override = {};
        }
	}
	return json::Object({
	    {"custom_fees",json::Value(json::object,
	            _custom_fees.begin(),_custom_fees.end(),[&](const auto &x){
	      auto iter = state.find(x.first);
	      if (iter == state.end()) return json::Value();
	      else return json::Value(x.first, x.second);
	    })
	    }
	});	
}

void Simulator::restoreSettings(json::Value v) {
    json::Value f = v["custom_fees"];
    for (json::Value x: f) {
        auto k = x.getKey(); 
        double fees = x.getNumber();
        _custom_fees[k] = fees;
        auto iter = state.find(k);
        if (iter != state.end()) {
            iter->second.fee_override = fees*0.01;
        }
        
    }    
}


void Simulator::reset(const std::chrono::system_clock::time_point &tp) {
	std::lock_guard _(lock);
	if (lastReset < tp) {
		lastReset = tp;
		std::vector<std::string> todel;
		for (auto &st: state) {
			st.second.source->reset(tp);
			simulate(st.second);
			if (++st.second.idle>10) todel.push_back(st.first);
		}
		for (const auto &n: todel) state.erase(n);
	}

}

IBrokerControl::AllWallets Simulator::getWallet() {
	std::lock_guard _(lock);
	const std::string_view names[2] = {
			"spot","futures"
	};
	AllWallets aw;

	for (std::size_t cnt = std::distance(std::begin(wallet),std::end(wallet)), i = 0; i < cnt; i++) {
		Wallet &w = wallet[i];
		IBrokerControl::Wallet ow;
		ow.walletId = names[i];
		for (const auto &x: w) ow.wallet.push_back({x.first, x.second.first});
		aw.push_back(ow);
	}
	{
		IBrokerControl::Wallet ow;
		ow.walletId = "position";
		for (const auto &x: state) {
			if (x.second.minfo.leverage) {
				ow.wallet.push_back({x.second.pair, x.second.collateral.getPos()});
			}
		}
		aw.push_back(ow);
	}
	{
		IBrokerControl::Wallet ow;
		ow.walletId = "active markets";
		for (const auto &x: state) {
				ow.wallet.push_back({x.second.pair, static_cast<double>(x.second.idle)});
		}
		aw.push_back(ow);
	}
	return aw;

}

void Simulator::loadState(const AbstractPaperTrading::TradeState &st,json::Value state) {
	int chs = chooseWallet(st.minfo);
	Wallet &w = wallet[chs];
	json::Value a = state[st.minfo.asset_symbol];
	json::Value c = state[st.minfo.currency_symbol];
	if (a.defined()) {
		auto iter = w.find(st.minfo.asset_symbol);
		if (iter == w.end() || iter->second.second == false) {
			w[st.minfo.asset_symbol] = {a.getNumber(),true};
		}
	}
	if (c.defined()) {
		auto iter =  w.find(st.minfo.currency_symbol);
		if (iter == w.end() || iter->second.second == false) {
			w[st.minfo.currency_symbol] = {c.getNumber(), true};
		}
	}
}

AbstractPaperTrading::RawBalance Simulator::getRawBalance(const AbstractPaperTrading::TradeState &st) const {
	const Wallet &w = wallet[chooseWallet(st.minfo)];
	RawBalance x = {0,0};
	auto itr = w.find(st.minfo.asset_symbol);
	if (itr != w.end()) x.asset = itr->second.first;
	itr = w.find(st.minfo.currency_symbol);
	if (itr != w.end()) x.currency = itr->second.first;

	return x;

}

json::Value Simulator::saveState(const AbstractPaperTrading::TradeState &st) {
	int chs = chooseWallet(st.minfo);
	const Wallet &w = wallet[chs];
	json::Value a,c;
	auto itr = w.find(st.minfo.asset_symbol);
	if (itr != w.end()) a = itr->second.first;
	itr = w.find(st.minfo.currency_symbol);
	if (itr != w.end()) c = itr->second.first;
	return json::Object {
		{st.minfo.asset_symbol, a},
		{st.minfo.currency_symbol, c}
	};

}

AbstractPaperTrading::TradeState& Simulator::getState(const std::string_view &symbol) {
	auto f = state.find(symbol);
	if (f == state.end()) {

	    auto cfiter = _custom_fees.find(symbol);	    
	    
		SourceInfo ps = parseSymbol(symbol);
		TradeState ts;
		ts.pair = symbol;
		ts.src_pair = ps.pair;
		ts.minfo = ps.exchange->getMarketInfo(ps.pair);
		if (cfiter != _custom_fees.end()) {
		    ts.fee_override = cfiter->second*0.01;
		}
		if (ts.minfo.leverage) {
			ts.minfo.wallet_id="futures";
		} else {
			ts.minfo.wallet_id="spot";
		}
		ts.needLoadWallet = true;
		ts.source = ps.exchange;
		ts.ticker = ps.exchange->getTicker(ps.pair);
		Wallet &w = wallet[chooseWallet(ts.minfo)];
		if (w.find(ts.minfo.currency_symbol) == w.end()) {
			if (ts.minfo.leverage || w.find(ts.minfo.asset_symbol) == w.end()) {
				double c = getInitialBalance(ts.minfo.currency_symbol);
				if (c == 0 && !ts.minfo.leverage) {
					double a= getInitialBalance(ts.minfo.asset_symbol);
					if (a == 0) {
						w[ts.minfo.currency_symbol] = {100,false};
					} else {
						w[ts.minfo.asset_symbol] = {a,false};
					}
				} else {
					if (c == 0) c = 100;
					w[ts.minfo.currency_symbol] = {c,false};
				}
			}
		}

		f = state.insert({
			ts.pair, ts
		}).first;
	}
	f->second.idle = 0;
	return f->second;
}

void Simulator::updateWallet(const AbstractPaperTrading::TradeState &st, const std::string_view &symbol, double difference) {
	Wallet &w = wallet[chooseWallet(st.minfo)];
	auto iter = w.find(symbol);
	if (iter == w.end()) w.emplace(std::string(symbol), std::pair{difference,true});
	else {
		iter->second.first +=difference;
		iter->second.second = true;
	}
}

Simulator::SourceInfo Simulator::parseSymbol(const std::string_view &symbol) {
	auto sep = symbol.find(':');
	if (sep == symbol.npos) throw std::runtime_error("Invalid market symbol");
	std::string_view exchangeName = symbol.substr(0,sep);
	std::string_view pair = symbol.substr(sep+1);
	auto p = exchanges->getStock(exchangeName);
	if (p == nullptr) throw std::runtime_error("Invalid market symbol - data source is not available");
	IBrokerControl *bc = dynamic_cast<IBrokerControl *>(p.get());
	if (bc) {
		auto bi = bc->getBrokerInfo();
		if (bi.subaccounts) {
			IBrokerSubaccounts *sub = dynamic_cast<IBrokerSubaccounts *>(p.get());
			if (sub) {
				auto sub1 = sub->createSubaccount("__simulator__");
				if (sub1) p = PStockApi(sub1);
			}
		}
	}
	return {
		p, pair
	};
}

int Simulator::chooseWallet(const MarketInfo &minfo) {
	if (minfo.leverage) return wallet_futures; else return wallet_spot;
}

bool Simulator::isIdle(const std::chrono::_V2::system_clock::time_point &tp) const {
	std::lock_guard _(lock);
	return std::chrono::duration_cast<std::chrono::minutes>(tp - lastReset).count() >= 15;
}

void Simulator::unload() {
	std::lock_guard _(lock);
	state.clear();
	for (auto &w: wallet) w.clear();
}
