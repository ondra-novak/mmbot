/*
 * simulator.h
 *
 *  Created on: 25. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_SIMULATOR_H_
#define SRC_MAIN_SIMULATOR_H_
#include "ibrokercontrol.h"
#include "papertrading.h"
#include "brokerlist.h"
#include <shared/linear_map.h>

class Simulator: public AbstractPaperTrading,
				 public IBrokerControl,
				 public IBrokerSubaccounts,
				 public IHistoryDataSource,
				 public IBrokerInstanceControl
				 {
public:

	Simulator(AbstractBrokerList *exchanges, std::string &&sub = std::string());


	virtual bool isSubaccount() const override;
	virtual bool areMinuteDataAvailable(const std::string_view &asset,	const std::string_view &currency) override;
	virtual std::vector<std::string> getAllPairs() override;
	virtual IStockApi* createSubaccount(const std::string_view &subaccount) const override;
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method, const std::string_view &vpath, const IBrokerControl::PageData &pageData) override;
	virtual json::Value getSettings(const std::string_view &pairHint) const override;
	virtual uint64_t downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
			const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
			std::vector<IHistoryDataSource::OHLC> &data) override;
	virtual IBrokerControl::BrokerInfo getBrokerInfo() override;
	virtual json::Value getMarkets() const override;
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override;
	virtual json::Value setSettings(json::Value v) override;
	virtual void restoreSettings(json::Value v) override;
	virtual void reset(const std::chrono::system_clock::time_point &tp) override;
	virtual IBrokerControl::AllWallets getWallet() override;
	virtual bool isIdle(
			const std::chrono::_V2::system_clock::time_point &tp) const override;
	virtual void unload()override;
	virtual void batchPlaceOrder(const std::vector<IStockApi::NewOrder> &orders,
			std::vector<json::Value> &ret_ids,
			std::vector<std::string> &ret_errors) override;
protected:
	virtual void loadState(const AbstractPaperTrading::TradeState &st, json::Value state) override;
	virtual AbstractPaperTrading::RawBalance getRawBalance(const AbstractPaperTrading::TradeState &st) const override;
	virtual json::Value saveState(const AbstractPaperTrading::TradeState &st) override;
	virtual AbstractPaperTrading::TradeState& getState(const std::string_view &symbol) override;
	virtual void updateWallet(const TradeState &st, const std::string_view &symbol, double difference) override;


protected:
	///We can store pointer only, because this broker is always part of the exchanges itself
	///So it is not possible, that broker will have invalid address
	AbstractBrokerList *exchanges;
	std::string sub;

	struct TradeState: AbstractPaperTrading::TradeState {
		int idle = 0;
	};

	using ActiveTradeStates = ondra_shared::linear_map<std::string, TradeState, std::less<std::string_view> >;
	using Wallet = ondra_shared::linear_map<std::string, std::pair<double, bool>, std::less<std::string_view>>;

	static const int wallet_spot = 0;
	static const int wallet_futures = 1;

	static int chooseWallet(const MarketInfo &minfo);

	ActiveTradeStates state;
	Wallet wallet[2];
	mutable json::Value allPairs;
	std::chrono::system_clock::time_point lastReset;

	struct SourceInfo {
		PStockApi exchange;
		std::string_view pair;
	};

	SourceInfo parseSymbol(const std::string_view &symbol);

	PStockApi findSuitableHistoryBroker(const std::string_view &asset, const std::string_view &currency);
};




#endif /* SRC_MAIN_SIMULATOR_H_ */
