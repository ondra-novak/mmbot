/*
 * localdailyperfmod.cpp
 *
 *  Created on: 25. 10. 2019
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <ctime>
#include <unordered_set>
#include <unordered_map>

#include "localdailyperfmod.h"

#include "../shared/stringview.h"
using ondra_shared::logError;
using ondra_shared::StrViewA;

#include "../shared/logOutput.h"

std::size_t LocalDailyPerfMonitor::daySeconds = 86400;

LocalDailyPerfMonitor::LocalDailyPerfMonitor(PStorage &&storage, std::string logfile,bool ignore_simulator)
	:storage(std::move(storage)), logfile(logfile),ignore_simulator(ignore_simulator)
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
	if (!report.simulator || ignore_simulator) {

		checkInit();

		json::Object sentence;
		sentence.set("currency",report.currency);
		sentence.set("change", report.change);

		json::Value(sentence).toStream(logf);
		logf.put('\n');
		logf.flush();
	}

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

	std::vector<double> sum;
	std::vector<unsigned int> cnt;
	std::vector<double> avg;
	sum.resize(header.size(),0);
	cnt.resize(header.size(),0);
	avg.resize(header.size(),0);


	Value jheader (json::array, header.begin(), header.end(), [](StrViewA x){return Value(std::string_view(x));});
	jheader.unshift("Date");
	Array reportrows;
	for (Value row: dailySums) {
		Value data = row[1];
		Array rrow;
		rrow.push_back(row[0].getUIntLong()*daySeconds);
		unsigned int idx = 0;
		for (auto &h : header) {
			double v = data[h].getNumber();
			sum[idx]+=v;
			if (v) cnt[idx]++;
			idx++;
			rrow.push_back(v);

		}
		reportrows.push_back(rrow);
	}

	std::transform(sum.begin(), sum.end(), cnt.begin(), avg.begin(),[](double a, unsigned int b) {
		return a / b;
	});

	report = Object({
		{"hdr", jheader},
		{"rows", reportrows},
		{"sums", Value(json::array, sum.begin(), sum.end(), [](double x){return x;})},
		{"avg", Value(json::array, avg.begin(), avg.end(), [](double x){return x;})}
	});


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

struct Sum {
	double val = 0;
};

void LocalDailyPerfMonitor::aggregate(unsigned int curDayIndex) {

	std::unordered_map<std::string, Sum> cur_sum;


	struct Position {
		std::string currency;
		double price = 0;
		double pos = 0;
		bool hit = false;
	};

	logf.close();
	try {

		std::ifstream inf(logfile);
		int i;
		while (( i = inf.get())!= EOF) {
			if (isspace(i)) continue;
			inf.putback(i);
			json::Value row = json::Value::fromStream(inf);
			std::string currency = row["currency"].getString();
			double change = row["change"].getNumber();

			cur_sum[currency].val+=change;
		}

		json::Object curs;
		for (auto &&t: cur_sum) {
			if (t.second.val) {
				curs.set(t.first, t.second.val);
			}
		}
		dailySums.push({curDayIndex, curs});
		dayIndex = curDayIndex;

		save();
		inf.close();
		logf.clear(std::ios::badbit|std::ios::eofbit);

		logf.open(logfile, std::ios::out| std::ios::trunc);
		prepareReport();

	} catch (std::exception &e) {
		logError("failed to flush daily performance data - $1", e.what());
		logf.open(logfile, std::ios::app);
		return;
	}
}

void LocalDailyPerfMonitor::save() {
	storage->store(json::Object({{"day", dayIndex},{"sum", dailySums}}));
}
