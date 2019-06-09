/*
 * report.h
 *
 *  Created on: 17. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_REPORT_H_
#define SRC_MAIN_REPORT_H_

#include <string_view>
#include "istockapi.h"
#include "storage.h"
#include "../shared/linear_map.h"
#include "../shared/stringview.h"

namespace json {
	class Array;
	class Object;
	class Value;
}

class Report {
public:
	using StoragePtr = PStorage;
	Report(StoragePtr &&report, std::size_t interval_in_ms )
		:report(std::move(report)),interval_in_ms(interval_in_ms) {}


	void genReport();

	using StrViewA = ondra_shared::StrViewA;
	template<typename T> using StringView = ondra_shared::StringView<T>;
	void setOrders(StrViewA symb, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	  const std::optional<IStockApi::Order> &sell);
	void setTrades(StrViewA symb, double current_balance, StringView<IStockApi::Trade> trades);
	void setInfo(
			StrViewA symb,
			StrViewA title,
			StrViewA assetSymb,
			StrViewA currencySymb,
			bool emulated);
	void setPrice(StrViewA symb, double price);

protected:



	struct OKey {
		StrViewA symb;
		int dir;
	};

	struct OValue {
		double price;
		double size;
	};

	struct OKeyCmp {
		bool operator()(const OKey &a, const OKey &b) const;
	};

	using OrderMap = ondra_shared::linear_map<OKey,OValue, OKeyCmp>;
	using TradeMap = ondra_shared::linear_map<StrViewA, json::Value>;
	using TitleMap = ondra_shared::linear_map<StrViewA, json::Value>;
	using PriceMap = ondra_shared::linear_map<StrViewA, double>;

	OrderMap orderMap;
	TradeMap tradeMap;
	TitleMap titleMap;
	PriceMap priceMap;

	StoragePtr report;


	void exportCharts(json::Object&& out);
	void exportOrders(json::Array &&out);
	void exportTitles(json::Object &&out);
	void exportPrices(json::Object &&out);
	std::size_t interval_in_ms;
};


#endif /* SRC_MAIN_REPORT_H_ */
