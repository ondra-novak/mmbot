/*
 * trade_report.h
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_RPTBROKER_TRADE_REPORT_H_
#define SRC_BROKERS_RPTBROKER_TRADE_REPORT_H_
#include <set>
#include <vector>

#include "aggregate.h"

#include "database.h"

class TradeReport {
public:

	using Day = DataBase::Day;
	using Date = Day;
	using SymbolMap = std::map<std::string_view, double>;

	TradeReport(DataBase &db);

	struct StandardReport {
		std::vector<std::pair<Day, SymbolMap> > months;
		std::vector<std::pair<Day, SymbolMap> > lastMonth;
		SymbolMap total;
	};

	StandardReport generateReport(const Date &today) ;

	void invalidate(std::uint64_t tm);
	void reset();


protected:
	using SymbolSet = std::set<std::string, std::less<> >;

	class Months: public AbstractAggregate<Day, SymbolMap, Day::Cmp> {
	public:
		Months(TradeReport &owner):owner(owner) {}
		using AggRes = typename AbstractAggregate<Day, SymbolMap, Day::Cmp>::AggRes;
		virtual AggRes reduce(const Day &month) const override;
		TradeReport &owner;
	};

	class Days: public AbstractAggregate<Day, SymbolMap, Day::Cmp> {
	public:
		Days(TradeReport &owner):owner(owner) {};
		using AggRes = typename AbstractAggregate<Day, SymbolMap, Day::Cmp>::AggRes;
		virtual AggRes reduce(const Day &day) const override;
		TradeReport &owner;
	};

	std::string_view storeSymbol(const std::string_view &symbol) const;

	DataBase &db;
	mutable SymbolSet sset;
	Months months;
	Days days;

	SymbolMap buildMap(std::streampos from, std::streampos to) const;
	SymbolMap merge(const SymbolMap &a, const SymbolMap &b) const;
	void rereduce(SymbolMap &a, const SymbolMap &b) const;
};

#endif /* SRC_BROKERS_RPTBROKER_TRADE_REPORT_H_ */
