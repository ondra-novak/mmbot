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
	struct AggrVal {
		double rpnl = 0;
		double eq = 0;
		AggrVal &operator+=(const AggrVal &v) {rpnl += v.rpnl; eq += v.eq;return *this;}
		AggrVal operator+(const AggrVal &v) const {return {rpnl + v.rpnl, eq + v.eq};}
	};
	using SymbolMap = std::map<std::string_view, AggrVal>;

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
	template<typename Fn>
	void aggrQuery(Fn &&fn, const Date &start, const Date &end, const Filter &filter);


	struct Options {
		std::vector<std::string_view> assets, currencies, brokers;
		std::vector<std::pair<std::uint64_t, std::uint64_t> > traders;
	};

	Options getOptions() const;

	friend SymbolMap &operator+=(SymbolMap &a, const SymbolMap &b);

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
		if (filter.magic.has_value() && hdr.magic != *filter.magic) return true;
		if (!nfo) {
			nfo = db.findTrader(hdr);
			if (!nfo) return true;
		}
		return fn(pos,hdr,trade, *nfo);
	});
}

template<typename Fn>
inline void TradeReport::aggrQuery(Fn &&fn, const Date &start, const Date &end, const Filter &flt) {
	if (!flt.asset.has_value() && !flt.broker.has_value() && !flt.uid.has_value() && !flt.magic.has_value()) {
		SymbolMap tmp;
		for (auto iter = days.lower_bound(start), iend = days.upper_bound(end); iter != iend; ++iter) {
			const SymbolMap &smap = days.update(iter);
			if (flt.currency.has_value()) {
				auto c = smap.find(*flt.currency);
				if (c == smap.end()) tmp[*flt.currency] =AggrVal{};
				else tmp[*flt.currency] = c->second;
				if (!fn(iter->first, tmp)) break;
			} else {
				if (!fn(iter->first, smap)) break;
			}
		}
	} else {
		for (auto iter = days.lower_bound(start), iend = days.upper_bound(end); iter != iend; ++iter) {
			Day a = iter->first;
			Day b = a;
			b.day+=1;
			SymbolMap tmp;
			query([&](off_t, const DataBase::Header &hdr, const DataBase::Trade &trd, const DataBase::TraderInfo &nfo){
				tmp[nfo.currency] += AggrVal{trd.rpnl, trd.change};
				return true;
			},a,b,0,flt);
			if (!tmp.empty() && !fn(iter->first, tmp)) break;
		}
	}
}

#endif /* SRC_BROKERS_RPTBROKER_TRADE_REPORT_H_ */
