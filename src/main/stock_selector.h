/*
 * stock_selector.h
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include <map>
#include <shared/ini_config.h>
#include <shared/shared_object.h>
#include <shared/linear_map.h>

#include "brokerlist.h"
#include "istockapi.h"



class StockSelector: public AbstractBrokerList{
public:
	using StockMarketMap =  ondra_shared::linear_map<std::string, PStockApi, std::less<>>;
	using TemporaryStockMap = std::map<std::string, PStockApi, std::less<> >;
	using PTemporaryStockMap = ondra_shared::SharedObject<TemporaryStockMap>;

	StockMarketMap stock_markets;
	PTemporaryStockMap temp_markets;

	virtual PStockApi get_broker(const std::string_view &name) override;
	virtual void enum_brokers(AbstractBrokerList::EnumFn &&fn) const override;
	StockSelector();

	void loadBrokers(const ondra_shared::IniConfig::Section &ini, bool test, int brk_timeout);
	bool checkBrokerSubaccount(const std::string &name);
	void clear();
	void housekeepingIdle(const std::chrono::system_clock::time_point &now);
	void appendSimulator();
};

