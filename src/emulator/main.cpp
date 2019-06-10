
#include <curlpp/Easy.hpp>
#include <memory>
#include <chrono>
#include <sstream>
#include <curlpp/Options.hpp>

#include "../brokers/api.h"
#include "../main/sgn.h"
#include "../shared/ini_config.h"

class DoubleZ {
public:
	double v;
	DoubleZ():v(0) {}
	DoubleZ(double v):v(v) {}
	operator double() const {return v;}
};

using BalanceMap = ondra_shared::linear_map<std::string, DoubleZ, std::less<std::string_view> >;


class IDataSource {
public:

	virtual IStockApi::Ticker getTicker(const std::string_view &pair) = 0;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) = 0;

};

using PDataSource = std::unique_ptr<IDataSource>;

auto now() {
	return std::size_t(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
		).count());
}

class Interface : public AbstractBrokerAPI {
public:
	Interface(PDataSource &&src, double fees)
		:datasrc(std::move(src))
		,fees(fees)
		,idgen(now()) {}

protected:

	PDataSource datasrc;
	BalanceMap balanceMap;
	double fees;

	using TradeMap  = ondra_shared::linear_map<std::string, TradeHistory, std::less<std::string_view> >;
	using OrderMap  = ondra_shared::linear_map<std::string, Orders, std::less<std::string_view>  >;


	TradeMap tradeMap;
	OrderMap orderMap;

	std::size_t idgen;


	virtual double getBalance(const std::string_view & symb)  {
		return balanceMap[std::string(symb)];
	}
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) {
		TradeHistory &hst = tradeMap[pair];
		auto from = std::find_if(hst.begin(), hst.end(), [&](const Trade &tr) {
			return tr.id == lastId;
		});
		if (from != hst.end()) ++from;
		return TradeHistory(from, hst.end());
	}
	virtual Orders getOpenOrders(const std::string_view & par) {
		return orderMap[par];
	}
	virtual Ticker getTicker(const std::string_view & pair) {
		Ticker tk = datasrc->getTicker(pair);
		Orders &o = orderMap[pair];
		TradeHistory &trs = tradeMap[pair];
		Orders newo;
		for (auto &&ord: o) {
			if ((ord.price - tk.last) * ord.size >= 0) {
				unsigned int id = ++idgen;
				double effprice = ord.price / (1-sgn(ord.size)*fees);
				trs.push_back(Trade{
					id,
					now(),
					ord.size,
					ord.price,
					ord.size,
					effprice,
				});
				double assch = ord.size;
				double curch = -ord.size * effprice;
				MarketInfo minfo = datasrc->getMarketInfo(pair);
				DoubleZ ba = balanceMap[minfo.asset_symbol];
				DoubleZ bc = balanceMap[minfo.currency_symbol];
				ba.v += assch;
				bc.v += curch;
			} else {
				newo.push_back(std::move(ord));
			}
		}
		std::swap(o, newo);
		return tk;
	}
	virtual json::Value placeOrder(const std::string_view & pair, const Order &order) {
		Orders &o = orderMap[pair];
		if (order.id.defined()) {
			auto iter = std::find_if(o.begin(), o.end(), [&](const Order &o) {
				return o.id == order.id;
			});
			if (iter == o.end()) throw Exception("Order not found");
			o.erase(iter);
		}
		Order no(order);
		no.id = ++idgen;
		o.push_back(no);
		return no.id;

	}
	virtual bool reset() {
		return true;
	}
	virtual MarketInfo getMarketInfo(const std::string_view & pair) {
		auto minfo =  datasrc->getMarketInfo(pair);
		minfo.fees = fees;
		return minfo;

	}
	///Retrieves trading fees
	/**
	 *
	 * @param pair trading pair
	 * @return MAKER fees (the MMBot doesn't generate TAKER's orders)
	 */
	virtual double getFees(const std::string_view &)  {
		return fees;
	}

	///Retrieve all available pairs
	virtual std::vector<std::string> getAllPairs() {
		return {};
	}
};


class CryptoWatch: public IDataSource {
public:

	virtual IStockApi::Ticker getTicker(const std::string_view &pair) {
		auto resp = request({"markets", pair,"price"});
		IStockApi::Ticker t;
		t.last = resp["result"]["price"].getNumber();
		t.ask = t.last * 1.0001;
		t.bid = t.last * 0.9999;
		t.time = now();
		return t;
	}
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) {
		auto miter = marketMap.find(pair);
		if (miter != marketMap.end()) return miter->second;

		auto r1 = request({"markets",pair});
		auto pair_symb = r1["result"]["pair"].getString();
		auto r2 = request({"pairs", pair_symb});
		auto base_symb = r2["result"]["base"]["symbol"];
		auto quote_symb = r2["result"]["quote"]["symbol"];


		IStockApi::MarketInfo minfo;
		minfo.asset_step = 1e-8;
		minfo.currency_step = 1e-8;
		minfo.asset_symbol = base_symb.getString();
		minfo.currency_symbol = quote_symb.getString();
		minfo.feeScheme = IStockApi::currency;
		minfo.fees = 0;
		minfo.min_size = 1e-8;
		minfo.min_volume = 0;
		marketMap[pair] = minfo;
		return minfo;
	}


protected:



	cURLpp::Easy curl_handle;
	using MarketMap = ondra_shared::linear_map<std::string, IStockApi::MarketInfo, std::less<std::string_view> >;

	MarketMap marketMap;


	json::Value request(std::initializer_list<std::string_view> path) {
		std::ostringstream urlbuilder;
		urlbuilder << "https://api.cryptowat.ch";
		for (auto &&p:path) urlbuilder << "/" << p;

		std::ostringstream response;
		curl_handle.reset();

		curl_handle.setOpt(cURLpp::Options::Url(urlbuilder.str()));
		curl_handle.setOpt(cURLpp::Options::WriteStream(&response));
		curl_handle.perform();

		return json::Value::fromString(response.str());
	}


};


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "No config given, terminated" << std::endl;
		return 1;
	}

	try {

		ondra_shared::IniConfig ini;

		ini.load(argv[1]);

		auto settings = ini["settings"];
		double fees = settings.mandatory["fees"].getNumber();

		Interface ifc(std::make_unique<CryptoWatch>(), fees);

		ifc.dispatch();

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 2;
	}
}

