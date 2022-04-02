/*
 * backtest2.cpp
 *
 *  Created on: 30. 3. 2022
 *      Author: ondra
 */


#include "backtest2.h"

#include "papertrading.h"

class Backtest::Source: public IStockApi {
public:

	Source(const MarketInfo &minfo, double currency, double assets);
	void setPrice(double cur_price, std::uint64_t time);
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual IStockApi::TradesSync syncTrades(json::Value lastId, const std::string_view &pair) override;
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size,
			double price, json::Value clientId, json::Value replaceId,
			double replaceSize) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual void reset(const std::chrono::_V2::system_clock::time_point &tp) override;
	virtual void batchPlaceOrder(const IStockApi::OrderList &orders,
			IStockApi::OrderIdList &ret_ids,
			IStockApi::OrderPlaceErrorList &ret_errors) override;

protected:
	MarketInfo minfo;
	double cur_price = 0;
	std::uint64_t time = 0;
	double assets;
	double currency;
};

class Backtest::Reporting: public IStatSvc {
public:
	Reporting(Backtest &owner):owner(owner) {}

	virtual void reportTrades(double finalPos, ondra_shared::StringView<IStatSvc::TradeRecord> trades) override;
	virtual void reportError(const IStatSvc::ErrorObj &errorObj) override;
	virtual std::size_t getHash() const override;
	virtual void reportMisc(const IStatSvc::MiscData &miscData, bool initial) override;
	virtual void reportOrders(int n, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	     const std::optional<IStockApi::Order> &sell) override;
	virtual void reportPrice(double price) override;
	virtual void reportPerformance(const PerformanceReport &repItem) override;
	virtual void setInfo(const IStatSvc::Info &info) override;

protected:
	Backtest &owner;
};


void Backtest::Source::setPrice(double cur_price, std::uint64_t time) {
	this->cur_price = cur_price;
	this->time = time;
}

double Backtest::Source::getBalance(const std::string_view &symb, const std::string_view &pair) {
	if (symb == minfo.asset_symbol) return assets;
	else if (symb == minfo.currency_symbol) return currency;
	else return 0.0;
}

IStockApi::TradesSync Backtest::Source::syncTrades(json::Value lastId, const std::string_view &pair) {
	return {};
}

IStockApi::MarketInfo Backtest::Source::getMarketInfo(const std::string_view &pair) {
	return minfo;
}

IStockApi::Orders Backtest::Source::getOpenOrders(const std::string_view &par) {
	return {};
}

json::Value Backtest::Source::placeOrder(const std::string_view &pair,
		double size, double price, json::Value clientId, json::Value replaceId,
		double replaceSize) {
	throw std::runtime_error("not supported");
}

IStockApi::Ticker Backtest::Source::getTicker(const std::string_view &piar) {
	return {
		cur_price, cur_price, cur_price, time
	};
}

void Backtest::Source::reset(const std::chrono::_V2::system_clock::time_point &tp) {
	//empty
}

Backtest::Source::Source(const MarketInfo &minfo, double currency, double assets):minfo(minfo),assets(assets),currency(currency) {

}

void Backtest::Source::batchPlaceOrder(
		const IStockApi::OrderList &orders, IStockApi::OrderIdList &ret_ids,
		IStockApi::OrderPlaceErrorList &ret_errors) {
	throw std::runtime_error("not supported");
}

Backtest::Backtest(const Trader_Config_Ex &cfg,
		 const IStockApi::MarketInfo &minfo,
		 double assets,
		 double currency)
:cfg(cfg)
,source(std::make_shared<Source>(minfo, currency,assets))
{
}

void Backtest::start(std::vector<double> &&prices, std::uint64_t start_time) {
	this->prices=std::move(prices);
	this->pos=0;
	cfg.paper_trading = true;

	trader=std::make_unique<Trader>(cfg,Trader_Env{
		StrategyRegister::getInstance().create(cfg.strategy_id, cfg.strategy_config),
		SpreadRegister::getInstance().create(cfg.spread_id, cfg.spread_config),
		source,
		std::make_unique<Reporting>(*this),
		nullptr,
		nullptr,
		nullptr,
		PWalletDB::make(),
		PWalletDB::make(),
		PBalanceMap::make(),
		PBalanceMap::make(),
		PBalanceMap::make(),
	});
	this->start_time = start_time;
	trader->get_exchange().reset(std::chrono::system_clock::now());
}

bool Backtest::next() {
	if (pos >= prices.size()) return false;
	Source *src = static_cast<Source *>(source.get());
	trader->get_exchange().reset(std::chrono::system_clock::now());
	src->setPrice(prices[pos], get_cur_time());
	trader->run();
	++pos;
	return true;
}

inline void Backtest::Reporting::reportTrades(double finalPos, ondra_shared::StringView<IStatSvc::TradeRecord> trades) {
	owner.position = finalPos;
	owner.trades = trades;
}

inline void Backtest::Reporting::reportError( const IStatSvc::ErrorObj &errorObj) {
	owner.buy_err = errorObj.buyError;
	owner.sell_err = errorObj.sellError;
	owner.gen_err = errorObj.genError;

}

inline std::size_t Backtest::Reporting::getHash() const {
	return 100;
}

inline void Backtest::Reporting::reportMisc(const IStatSvc::MiscData &miscData, bool ) {
	owner.miscData = miscData;
}

inline void Backtest::Reporting::reportOrders(int, const std::optional<IStockApi::Order> &buy,
	  	  	     	 	 	 	 	 	 	 	 	 const std::optional<IStockApi::Order> &sell) {
	owner.buy = buy;
	owner.sell = sell;
}

inline void Backtest::Reporting::reportPrice(double price) {
	owner.cur_price = price;
}

inline void Backtest::Reporting::reportPerformance(const PerformanceReport &) {
	//empty
}

inline void Backtest::Reporting::setInfo(const IStatSvc::Info &info) {
	owner.info = info;

}

Trader& Backtest::get_trader() {
	return *trader;
}

std::uint64_t Backtest::get_cur_time() const {
	return start_time+static_cast<std::uint64_t>(pos)*60000;
}
