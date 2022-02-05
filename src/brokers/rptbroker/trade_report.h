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

	struct Filter {
		std::optional<std::string> asset, currency, broker;
		std::optional<std::size_t> uid, magic;
		bool skip_deleted = false;
	};

	template<typename Fn>
	void query(Fn &&fn, const Date &start, const Date &end, off_t cursor, const Filter &filter);


	struct Options {
		std::vector<std::string_view> assets, currencies, brokers;
		std::vector<std::pair<std::uint64_t, std::uint64_t> > traders;
	};

	Options getOptions() const;

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

template<typename Fn>
inline void TradeReport::query(Fn &&fn, const Date &start, const Date &end, off_t cursor, const Filter &filter) {
	off_t pos = db.findDay(start);
	off_t pend = db.findDay(end);
	pos = std::max(pos, cursor);
	bool needTrader = filter.asset.has_value() || filter.currency.has_value() || filter.broker.has_value();
	db.scanTradesFrom(pos, [&](off_t pos, const DataBase::Header &hdr, const DataBase::Trade &trade){
		if (pos >= pend) return false;
		if (trade.deleted && filter.skip_deleted) return true;
		const DataBase::TraderInfo *nfo = nullptr;
		if (needTrader) {
			nfo = db.findTrader(hdr);
			if (!nfo) return true;
			if (filter.asset.has_value() && nfo->getAsset() != *filter.asset) return true;
			if (filter.currency.has_value() && nfo->getCurrency() != *filter.currency) return true;
			if (filter.broker.has_value() && nfo->getBroker() != *filter.broker) return true;
		}
		if (filter.uid.has_value() && hdr.uid != *filter.uid) return true;
		if (filter.magic.has_value() && hdr.uid != *filter.magic) return true;
		if (!nfo) {
			nfo = db.findTrader(hdr);
			if (!nfo) return true;
		}
		return fn(pos,hdr,trade, *nfo);
	});
}

#endif /* SRC_BROKERS_RPTBROKER_TRADE_REPORT_H_ */
