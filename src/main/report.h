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
#include <shared/linear_map.h>
#include <shared/shared_object.h>
#include <shared/stdLogOutput.h>
#include <shared/stringview.h>
#include <userver/callback.h>
#include "traderecord.h"

#include "istockapi.h"
#include "storage.h"
#include "istatsvc.h"
#include "strategy.h"

namespace json {
	class Array;
	class Object;
	class Value;
}


class Report {


public:
	struct StreamData {
		bool command;
		json::Value event;
		std::size_t hdr_hash;
		std::size_t data_hash;
		void set_event(const json::Value &hdr, const json::Value &data);
	};

	static StreamData ev_clear_cache;
	static StreamData ev_refresh;
	static StreamData ev_end_refresh;
	static StreamData ev_update;
	static StreamData ev_ping;

	using Stream = userver::Callback<bool(const StreamData &)>;

	using StoragePtr = PStorage;
	using MiscData = IStatSvc::MiscData;
	using ErrorObj = IStatSvc::ErrorObj;
	using InfoObj = IStatSvc::Info;
	using Sync = std::unique_lock<std::recursive_mutex>;

	Report(StoragePtr &&report)
		:report(std::move(report)),interval_in_ms(30L*86400000L)
		,counter(initCounter())
		,revize(1),refresh_after_clear(true)
	{}


	void addStream(Stream &&stream);
	void pingStreams();

	void setInterval(std::uint64_t interval);
	void calcWindowOnly(bool en);
	void genReport();
	json::Value genReport_noStore();

	template<typename T> using StringView = ondra_shared::StringView<T>;
	void setOrders(std::size_t rev, std::string_view symb, int n, const std::optional<IStockApi::Order> &buy,
			  	  	  	  	  	  const std::optional<IStockApi::Order> &sell);
	void setTrades(std::size_t rev, std::string_view symb, double finalPos, bool inverted,  StringView<TradeRecord> trades);
	void setInfo(std::size_t rev, std::string_view symb, const InfoObj &info);
	void setMisc(std::size_t rev, std::string_view symb, const MiscData &miscData, bool initial);

	void setOrder(std::size_t rev, std::string_view symb, int n, double price, double size, double pl, double np);
	void setPrice(std::size_t rev, std::string_view symb, double price, double pl, double np);


	void addLogLine(std::string_view ln);
	void clear();

	void perfReport(json::Value report);
	void setNewsMessages(unsigned int count);

	void reportLogMsg(std::size_t rev, const std::string_view &symb, std::uint64_t timestamp, const std::string_view &text);

	void setError(std::size_t rev,std::string_view symb, const ErrorObj &errorObj);

	static ondra_shared::PStdLogProviderFactory captureLog(const ondra_shared::SharedObject<Report> &rpt, ondra_shared::PStdLogProviderFactory target);

	std::size_t getRev() const {
		return revize;
	}

protected:



	struct OKey {
		std::string symb;
		int dir;
	};

	struct OValue {
		double price;
		double size;
		double pl;
		double np;
		json::Value toJson() const;
	};

	struct OKeyCmp {
		bool operator()(const OKey &a, const OKey &b) const;
	};

	using OrderMap = ondra_shared::linear_map<OKey,OValue, OKeyCmp>;
	using TradeMap = ondra_shared::linear_map<std::string, json::Value, std::less<> >;
	using InfoMap = ondra_shared::linear_map<std::string, json::Value, std::less<> >;
	using MiscMap = ondra_shared::linear_map<std::string, json::Value, std::less<> >;
	using PriceMap = ondra_shared::linear_map<std::string, json::Value, std::less<> >;

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

	void sendStream(const json::Value &hdr, const json::Value &data);


	void exportCharts(json::Object&& out);
	void exportOrders(json::Array &&out);
	void exportTitles(json::Object &&out);
	void exportPrices(json::Object &&out);
	void exportMisc(json::Object &&out);
	std::uint64_t interval_in_ms;
	bool calc_window_only = false;

	std::size_t counter;
	std::size_t revize;
	bool refresh_after_clear;
	std::string buff;


	static std::size_t initCounter();

	bool stream_refresh(Stream &stream) const ;


	template<typename ME> static void sendStreamOrder(ME &me, const OKey &key, const OValue &data);
	template<typename ME> static void sendStreamTrades(ME &me, const std::string_view &symb, const json::Value &records);
	template<typename ME> static void sendStreamInfo(ME &me, const std::string_view &symb, const json::Value &object);
	template<typename ME> static void sendStreamMisc(ME &me, const std::string_view &symb, const json::Value &object);
	template<typename ME> static void sendStreamPrice(ME &me, const std::string_view &symb, const json::Value &data);
	template<typename ME> static void sendStreamError(ME &me, const std::string_view &symb, const json::Value &obj);
	template<typename ME> void sendStreamGlobal(ME &me) const;
	template<typename ME> void sendNewsMessages(ME &me) const;
	template<typename ME> void sendLogMessages(ME &me) const;
};

using PReport = ondra_shared::SharedObject<Report>;



#endif /* SRC_MAIN_REPORT_H_ */
