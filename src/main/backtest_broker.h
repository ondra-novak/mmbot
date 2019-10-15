#include <cmath>

#include "../shared/stringview.h"
#include "istatsvc.h"
#include "istockapi.h"


class BacktestBroker: public IStockApi {
public:

	BacktestBroker(ondra_shared::StringView<IStatSvc::ChartItem> chart,
			const MarketInfo &minfo,
			double balance, bool mirror);
	virtual TradeHistory getTrades(json::Value lastId, std::uintptr_t fromTime, const std::string_view & pair) override;
	virtual Orders getOpenOrders(const std::string_view & par) override;
	virtual Ticker getTicker(const std::string_view & piar) override;
	virtual json::Value placeOrder(const std::string_view & pair,
			double size, double price,json::Value clientId,
			json::Value replaceId,double replaceSize) override;
	virtual bool reset() override ;
	virtual void testBroker() override {}
	virtual double getBalance(const std::string_view &) override;
	virtual bool isTest() const override {return false;}
	virtual MarketInfo getMarketInfo(const std::string_view &) override{
		return minfo;
	}
	virtual double getFees(const std::string_view &) override{
		return minfo.fees;
	}
	virtual std::vector<std::string> getAllPairs() override {return {};}

	double getScore() const {
		return currency+sqrt(chart[0].bid*chart[0].ask)*balance;
	}
	unsigned int getTradeCount() const {
		return std::min(buys,sells);
	}
	virtual BrokerInfo getBrokerInfo() override {
		return BrokerInfo {};
	}

protected:
	double currency=0;
	ondra_shared::StringView<IStatSvc::ChartItem> chart;
	TradeHistory trades;
	Order buy, sell;
	bool buy_ex = true, sell_ex = true;
	int pos;
	bool back ;
	MarketInfo minfo;
	double balance;
	double initial_balance;
	unsigned int buys=0, sells=0;

};
