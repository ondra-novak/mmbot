/*
 * localdailyperfmod.cpp
 *
 *  Created on: 25. 10. 2019
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <ctime>
#include <unordered_set>

#include "localdailyperfmod.h"

#include "../shared/stringview.h"
using ondra_shared::logError;
using ondra_shared::StrViewA;

#include "../shared/logOutput.h"

std::size_t LocalDailyPerfMonitor::daySeconds = 86400;

LocalDailyPerfMonitor::LocalDailyPerfMonitor(PStorage &&storage, std::string logfile)
	:storage(std::move(storage)), logfile(logfile)
{
}

void LocalDailyPerfMonitor::checkInit() {
	time_t t = std::time(nullptr);
	unsigned int newdayindex = t/daySeconds;

	if (!dailySums.defined()) {
		init(newdayindex);
	}
	if (newdayindex != dayIndex) {
		aggregate(newdayindex);
	}


}

void LocalDailyPerfMonitor::sendItem(const PerformanceReport &report) {
	checkInit();

	json::Object sentence;
	sentence.set
			("uid",report.uid)
			("currency",report.currency)
			("price",report.price)
			("size",report.size);

	json::Value(sentence).toStream(logf);
	logf.put('\n');
	logf.flush();

}

void LocalDailyPerfMonitor::prepareReport() {
	using namespace json;
	std::unordered_set<std::string_view> header;
	for (Value row: dailySums) {
		Value data = row[1];
		for (Value x:data) {
			header.insert(x.getKey());
		}
	}


	Value jheader (json::array, header.begin(), header.end(), [](StrViewA x){return x;});
	jheader.unshift("time");
	Array reportrows;
	for (Value row: dailySums) {
		Value data = row[1];
		Array rrow;
		rrow.push_back(row[0].getUInt()*daySeconds);
		for (auto &h : header) {
			rrow.push_back(data[h].getNumber());
		}
		reportrows.push_back(rrow);
	}

	report = Object
			("hdr", jheader)
			("rows", reportrows);



}


json::Value LocalDailyPerfMonitor::getReport()  {
	checkInit();

	return report;
}

void LocalDailyPerfMonitor::init(unsigned int curDayIndex) {
	json::Value data = storage->load();
	if (data.hasValue()) {
		dayIndex = data["day"].getUInt();
		dailySums = data["sum"];
	} else {
		dayIndex = curDayIndex;
		dailySums = json::array;
		save();
	}
	logf.open(logfile, std::ios::app);
	prepareReport();

}

void LocalDailyPerfMonitor::aggregate(unsigned int curDayIndex) {

	struct Position {
		std::string currency;
		double price = 0;
		double pos = 0;
		bool hit = false;
	};


	std::unordered_map<std::size_t, Position> positions;
	std::unordered_map<std::string, double> pldb;

	logf.close();
	try {

		std::ifstream inf(logfile);
		int i;
		while (( i = inf.get())!= EOF) {
			if (isspace(i)) continue;
			inf.putback(i);
			json::Value row = json::Value::fromStream(inf);

			std::size_t uid = row["uid"].getUInt();
			std::string currency = row["currency"].getString();
			double price = row["price"].getNumber();
			double size = row["size"].getNumber();

			Position &pos = positions[uid];
			double pl = (price - pos.price) * pos.pos;
			if (pl) pos.hit = true;
			pldb[currency] += pl;
			pos.currency = currency;
			pos.pos += size;
			pos.price = price;
		}

		json::Object curs;
		for (auto &&t: pldb) {
			if (t.second) {
				curs.set(t.first, t.second);
			}
		}
		dailySums.push({curDayIndex, curs});
		dayIndex = curDayIndex;

		save();
		inf.close();
		logf.clear(std::ios::badbit|std::ios::eofbit);

		logf.open(logfile, std::ios::out| std::ios::trunc);
		{
			for(auto &&t: positions) {
				  if (t.second.hit) {
					sendItem(PerformanceReport {
						0,t.first,"",t.second.currency,"",t.second.price,t.second.pos
					});
				  }
			}
		}
		logf.flush();
		prepareReport();

	} catch (std::exception &e) {
		logError("failed to flush daily performance data - $1", e.what());
		dayIndex = curDayIndex;
		logf.open(logfile, std::ios::app);
		return;
	}
}

void LocalDailyPerfMonitor::save() {
	storage->store(json::Object("day", dayIndex)("sum", dailySums));
}
