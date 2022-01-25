/*
 * papertrading.h
 *
 *  Created on: 16. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_PAPERTRADING_H_
#define SRC_MAIN_PAPERTRADING_H_

#include <map>
#include <mutex>
#include <optional>

#include "ibrokercontrol.h"
#include "acb.h"

#include "istockapi.h"

class AbstractPaperTrading: public IStockApi {
public:

	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;

	virtual IStockApi::TradesSync syncTrades(json::Value lastId,
			const std::string_view &pair) override;
	virtual double getFees(const std::string_view &pair) override;
	virtual IStockApi::Ticker getTicker(const std::string_view &piar) override;
	virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override;
	virtual json::Value placeOrder(const std::string_view &pair, double size, double price,
			json::Value clientId, json::Value replaceId, double replaceSize) override;






protected: //To override

	struct TradeState {
		PStockApi source;
		MarketInfo minfo;
		Orders openOrders;
		TradeHistory trades;
		Ticker ticker;
		std::string pair;
		ACB collateral = ACB(0,0,0);
		int orderCounter = 1;
		bool needLoadWallet = true;
	};

	virtual void processTrade(TradeState &st, const Trade &t);
	///Updates wallet (must be implemented);
	virtual void updateWallet(const std::string_view &symbol, double difference) = 0;

	///Get trade state from pair
	virtual TradeState &getState(const std::string_view &symbol) = 0;

	///called to save state of the trader
	virtual json::Value saveState(const TradeState &st)  = 0;
	///called to restore state
	virtual void loadState(const TradeState &st, json::Value state) = 0;

	struct RawBalance {
		double asset;
		double currency;
	};

	///Ask balance - without including collateral - this is need to calculate equity
	/**
	 * @param st state
	 * @param asset return asset instead
	 * @return balance
	 */
	virtual RawBalance getRawBalance(const TradeState &st) const = 0;

	virtual bool canCreateOrder(const TradeState &st, double price, double size);

protected:

	json::Value exportState(const TradeState &st);
	void importState(TradeState &st, json::Value v);

	void simulate(TradeState &state);

	mutable std::recursive_mutex lock;


};

class PaperTrading: public AbstractPaperTrading, public IBrokerControl {
public:

	PaperTrading(PStockApi source);
	virtual double getBalance(const std::string_view &symb, const std::string_view &pair);
	virtual bool reset();
	virtual json::Value getMarkets() const;
	virtual std::vector<std::string> getAllPairs();
	virtual void restoreSettings(json::Value v);
	virtual json::Value setSettings(json::Value v);
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method,
			const std::string_view &vpath, const IBrokerControl::PageData &pageData);
	virtual json::Value getSettings(const std::string_view &pairHint) const;
	virtual IBrokerControl::BrokerInfo getBrokerInfo();
	virtual IBrokerControl::AllWallets getWallet();
	virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &pair) override;




protected:


	TradeState state;
	double asset = 0;
	double currency = 0;
	bool asset_valid = false;
	bool currency_valid = false;

	virtual void loadState(const AbstractPaperTrading::TradeState &st,
			json::Value state);
	virtual TradeState& getState(const std::string_view &symbol);
	virtual json::Value saveState(const AbstractPaperTrading::TradeState &st);
	double getBalanceFromWallet(const std::string_view &symb);
	void updateWallet(const std::string_view &symbol, double difference) override;
	virtual RawBalance getRawBalance(const TradeState &st) const override;
};



#endif /* SRC_MAIN_PAPERTRADING_H_ */
