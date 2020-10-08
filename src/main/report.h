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
#include <optional>
#include "istockapi.h"
#include "storage.h"
#include "../shared/linear_map.h"
#include "../shared/shared_object.h"
#include "../shared/stdLogOutput.h"
#include "../shared/stringview.h"
#include "istatsvc.h"
#include "strategy.h"

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
	using Sync = std::unique_lock<std::recursive_mutex>;

	Report(StoragePtr &&report, std::size_t interval_in_ms)
		:report(std::move(report)),interval_in_ms(interval_in_ms)
		,counter(initCounter()){}


	void setInterval(std::uint64_t interval);
	void genReport();

	using StrViewA = ondra_shared::StrViewA;
	template<typename T> using StringView = ondra_shared::StringView<T>;
	void setOrders(StrViewA symb, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	  const std::optional<IStockApi::Order> &sell);
	void setTrades(StrViewA symb, StringView<IStatSvc::TradeRecord> trades);
	void setInfo(StrViewA symb, const InfoObj &info);
	void setMisc(StrViewA symb, const MiscData &miscData);

	void setPrice(StrViewA symb, double price);
	void addLogLine(StrViewA ln);
	void clear(StrViewA symb);
	void clear();

	void perfReport(json::Value report);

	virtual void setError(StrViewA symb, const ErrorObj &errorObj);

	static ondra_shared::PStdLogProviderFactory captureLog(const ondra_shared::SharedObject<Report> &rpt, ondra_shared::PStdLogProviderFactory target);


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
	json::Value perfRep;

	StoragePtr report;


	void exportCharts(json::Object&& out);
	void exportOrders(json::Array &&out);
	void exportTitles(json::Object &&out);
	void exportPrices(json::Object &&out);
	void exportMisc(json::Object &&out);
	std::uint64_t interval_in_ms;

	std::size_t counter;


	static std::size_t initCounter();



};



#endif /* SRC_MAIN_REPORT_H_ */
