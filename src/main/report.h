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

struct ReportConfig {
	std::size_t interval_in_ms;

};



class Report {


public:
	using Stream = std::function<bool(json::Value)>;

	using StoragePtr = PStorage;
	using MiscData = IStatSvc::MiscData;
	using ErrorObj = IStatSvc::ErrorObj;
	using InfoObj = IStatSvc::Info;
	using Sync = std::unique_lock<std::recursive_mutex>;

	Report(StoragePtr &&report, const ReportConfig &cfg)
		:report(std::move(report)),interval_in_ms(cfg.interval_in_ms)
		,counter(initCounter()){}

	void addStream(Stream &&stream);
	void pingStreams();

	void setInterval(std::uint64_t interval);
	void genReport();
	json::Value genReport_noStore();

	using StrViewA = ondra_shared::StrViewA;
	template<typename T> using StringView = ondra_shared::StringView<T>;
	void setOrders(StrViewA symb, int n, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	  const std::optional<IStockApi::Order> &sell);
	void setTrades(StrViewA symb, double finalPos,  StringView<IStatSvc::TradeRecord> trades);
	void setInfo(StrViewA symb, const InfoObj &info);
	void setMisc(StrViewA symb, const MiscData &miscData);

	void setPrice(StrViewA symb, double price);
	void addLogLine(StrViewA ln);
	void clear(StrViewA symb);
	void clear();

	void perfReport(json::Value report);
	void setNewsMessages(unsigned int count);

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
		json::Value toJson() const;
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
	std::vector<Stream> streams;
	unsigned int newsMessages = 0;

	void sendStream(const json::Value &v);


	void exportCharts(json::Object&& out);
	void exportOrders(json::Array &&out);
	void exportTitles(json::Object &&out);
	void exportPrices(json::Object &&out);
	void exportMisc(json::Object &&out);
	std::uint64_t interval_in_ms;

	std::size_t counter;


	static std::size_t initCounter();

	bool stream_refresh(Stream &stream) const ;


	template<typename ME> static void sendStreamOrder(ME &me, const OKey &key, const OValue &data);
	template<typename ME> static void sendStreamTrades(ME &me, const std::string_view &symb, const json::Value &records);
	template<typename ME> static void sendStreamInfo(ME &me, const std::string_view &symb, const json::Value &object);
	template<typename ME> static void sendStreamMisc(ME &me, const std::string_view &symb, const json::Value &object);
	template<typename ME> static void sendStreamPrice(ME &me, const std::string_view &symb, double data);
	template<typename ME> static void sendStreamError(ME &me, const std::string_view &symb, const json::Value &obj);
	template<typename ME> void sendStreamGlobal(ME &me) const;
	template<typename ME> void sendNewsMessages(ME &me) const;
	template<typename ME> void sendLogMessages(ME &me) const;
};



#endif /* SRC_MAIN_REPORT_H_ */
