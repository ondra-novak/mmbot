/*
 * report.h
 *
 *  Created on: 17. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_REPORT_H_
#define SRC_MAIN_REPORT_H_

#include <imtjson/array.h>
#include <string_view>
#include "istockapi.h"
#include "storage.h"
#include "../shared/linear_map.h"
#include "../shared/stdLogOutput.h"
#include "../shared/stringview.h"
#include "istatsvc.h"

namespace json {
	class Array;
	class Object;
	class Value;
}

class Report {
public:
	using StoragePtr = PStorage;
	using MiscData = IStatSvc::MiscData;
	using ErrorObj = IStatSvc::ErrorObj;
	using InfoObj = IStatSvc::Info;

	Report(StoragePtr &&report, std::size_t interval_in_ms, bool a2np )
		:report(std::move(report)),interval_in_ms(interval_in_ms),a2np(a2np) {}


	void genReport();

	using StrViewA = ondra_shared::StrViewA;
	template<typename T> using StringView = ondra_shared::StringView<T>;
	void setOrders(StrViewA symb, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	  const std::optional<IStockApi::Order> &sell);
	void setTrades(StrViewA symb, StringView<IStockApi::TradeWithBalance> trades, double neutral_pos);
	void setInfo(StrViewA symb, const InfoObj &info);
	void setMisc(StrViewA symb, const MiscData &miscData);

	void setPrice(StrViewA symb, double price);
	void addLogLine(StrViewA ln);

	virtual void setError(StrViewA symb, const ErrorObj &errorObj);

	ondra_shared::PStdLogProviderFactory captureLog(ondra_shared::PStdLogProviderFactory target);

protected:



	struct OKey {
		std::string symb;
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
	using TradeMap = ondra_shared::linear_map<std::string, json::Value>;
	using InfoMap = ondra_shared::linear_map<std::string, json::Value>;
	using MiscMap = ondra_shared::linear_map<std::string, json::Value>;
	using PriceMap = ondra_shared::linear_map<std::string, double>;

	OrderMap orderMap;
	TradeMap tradeMap;
	InfoMap infoMap;
	PriceMap priceMap;
	MiscMap miscMap;
	MiscMap errorMap;
	json::Array logLines;

	StoragePtr report;


	void exportCharts(json::Object&& out);
	void exportOrders(json::Array &&out);
	void exportTitles(json::Object &&out);
	void exportPrices(json::Object &&out);
	void exportMisc(json::Object &&out);
	std::size_t interval_in_ms;
	bool a2np;
};



#endif /* SRC_MAIN_REPORT_H_ */
