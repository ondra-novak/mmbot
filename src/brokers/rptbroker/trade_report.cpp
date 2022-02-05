/*
 * trade_report.cpp
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#include "trade_report.h"

TradeReport::TradeReport(DataBase &db):db(db),months(*this),days(*this) {

	reset();
}

TradeReport::SymbolMap TradeReport::buildMap(std::streampos from, std::streampos to) const {
	SymbolMap r;
	db.scanTradesFrom(from, [&](std::streampos pos, const DataBase::Header &hdr, const DataBase::Trade &trd){
		if (pos >= to) return false;
		if (!trd.deleted) {
			double v = trd.rpnl;
			if (v) {
				const DataBase::TraderInfo *tinfo = db.findTrader(hdr);
				if (tinfo) {
					auto s = tinfo->getCurrency();
					auto iter = r.find(s);
					if (iter == r.end()) {
						r.emplace(storeSymbol(s), v);
					} else {
						iter->second += v;
					}
				}
			}
		}
		return true;
	});
	return r;
}

TradeReport::Months::AggRes TradeReport::Months::reduce(const Day &month) const {
	Day d1 { month.year, month.month,0};
	Day d2 { month.year, month.month+1,0};
	auto from = owner.db.findDay(d1);
	auto to = owner.db.findDay(d2);
	return {owner.buildMap(from, to), true};
}

TradeReport::Days::AggRes TradeReport::Days::reduce(const Day &day) const {
	auto from = owner.db.findDay(day);
	Day d1 {day.year, day.month, day.day+1};
	auto to = owner.db.findDay(d1);
	return {owner.buildMap(from, to), true};
}

std::string_view TradeReport::storeSymbol(const std::string_view &symbol) const {
	auto iter = sset.find(symbol);
	if (iter == sset.end()) {
		iter = sset.insert(std::string(symbol)).first;
	}
	return *iter;
}

TradeReport::SymbolMap TradeReport::merge(const SymbolMap &a, const SymbolMap &b) const {
	SymbolMap r;
	auto ap = a.begin(), ae = a.end(), bp = b.begin(), be = b.end();
	while (ap != ae && bp != be) {
		if (ap->first < bp->first) {r.insert(*ap);++ap;}
		else if (ap->first > bp->first) {r.insert(*bp);++bp;}
		else {
			r.emplace(ap->first, ap->second+bp->second);
		}
	}
	while (ap != ae) {
		r.insert(*ap);++ap;
	}
	while (bp != be) {
		r.insert(*bp);++bp;
	}
	return r;
}

void TradeReport::rereduce(SymbolMap &a, const SymbolMap &b) const {
	for (const auto &x: b) {
		auto iter = a.find(x.first);
		if (iter == a.end()) a.insert(x);
		else iter->second += x.second;
	}
}

TradeReport::StandardReport TradeReport::generateReport(const Date &today)  {
	StandardReport rpt;
	for (auto iter = months.begin(); iter != months.upper_bound(today); ++iter) {
		const SymbolMap &smap = months.update(iter);
		rereduce(rpt.total, smap);
		rpt.months.push_back({
			iter->first, smap
		});
	}
	Date first_day = today; first_day.day = 1;
	for (auto iter = days.lower_bound(first_day); iter != days.lower_bound(today); ++iter) {
		const SymbolMap &smap = days.update(iter);
		rereduce(rpt.total, smap);
		rpt.lastMonth.push_back({
			iter->first, smap
		});
	}
	return rpt;

}

void TradeReport::invalidate(std::uint64_t tm) {
	Day d = Day::fromTime(tm);
	days.invalidate(d);
	months.invalidate(Day{d.year, d.month});
}

void TradeReport::reset() {

	for (const auto &x: db.days()) {
		days.invalidate(x.first);
		months.invalidate(x.first.getMonth());
	}

}
