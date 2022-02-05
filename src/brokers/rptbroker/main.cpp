/*
 * main.cpp
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#include <unistd.h>
#include <ctime>
#include <optional>
#include <iostream>
#include <imtjson/value.h>
#include <shared/default_app.h>
#include <imtjson/array.h>
#include <imtjson/object.h>

#include "trade_report.h"

#include "database.h"


json::Value readFromStream(std::istream &in) {
	using namespace json;
	int i = in.get();
	while (i != EOF) {
		if (!std::isspace(i)) {
			in.putback(i);
			Value req = Value::fromStream(in);
			return req;
		}
		i = in.get();
	}
	return json::undefined;
}

void storeTrade(DataBase &main, DataBase &backup, TradeReport &rep, json::Value trade, bool flush) {
	DataBase::Header hdr;
	DataBase::Trade tr;

	hdr.setTime(trade["time"].getUIntLong());
	hdr.uid = trade["uid"].getUIntLong();
	hdr.magic = trade["magic"].getUIntLong();
	tr.change = trade["change"].getNumber();
	tr.price= trade["price"].getNumber();
	tr.pos = trade["pos"].getValueOrDefault(std::numeric_limits<double>::quiet_NaN());
	tr.invert_price= trade["invert_price"].getBool();
	tr.size = trade["size"].getNumber();
	tr.deleted = trade["deleted"].getBool();
	tr.rpnl = trade["acbpnl"].getValueOrDefault(tr.change);
	tr.setTradeId(trade["tradeId"].getString());

	if (!main.findTrader(hdr)) {
		DataBase::TraderInfo nfo;
		nfo.setAsset(trade["asset"].getString());
		nfo.setCurrency(trade["currency"].getString());
		nfo.setBroker(trade["broker"].getString());
		main.addTrader(hdr, nfo);
		backup.addTrader(hdr, nfo);
	}
	main.addTrade(hdr, tr);
	backup.addTrade(hdr, tr);
	if (flush) {
		main.flush();
		backup.flush();
	}
	rep.invalidate(hdr.getTime());
}

static std::uint64_t monthToTimestamp(const TradeReport::Day &m) {
	std::tm t = {};
	t.tm_year = m.year;
	t.tm_mon = m.month+1; //day is 0 - calculates timestamp of the last day
	return timegm(&t);
}

static std::uint64_t dateToTimestamp(const TradeReport::Day &d) {
	std::tm t = {};
	t.tm_year = d.year;
	t.tm_mon = d.month;
	t.tm_mday = d.day;
	return timegm(&t);
}

json::Value genReport(TradeReport &rpt) {
	TradeReport::StandardReport srp = rpt.generateReport(TradeReport::Date::fromTime(
			std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
			).count()
	));
	std::vector<std::string_view> header;
	for (const auto &x: srp.total) header.push_back(x.first);
	json::Value jhdr (json::array, header.begin(), header.end(), [&](const std::string_view &x){return json::Value(x);});
	json::Array rows;
	jhdr.unshift("");
	for (const auto &row: srp.months) {
		json::Value r(json::array, header.begin(), header.end(),[&](const std::string_view &x){
			auto iter = row.second.find(x);
			if (iter == row.second.end()) return 0.0;
			else return iter->second;
		});
		r.unshift(monthToTimestamp(row.first));
		rows.push_back(r);
	}
	for (const auto &row: srp.lastMonth) {
		json::Value r(json::array, header.begin(), header.end(),[&](const std::string_view &x){
			auto iter = row.second.find(x);
			if (iter == row.second.end()) return 0.0;
			else return iter->second;
		});
		r.unshift(dateToTimestamp(row.first));
		rows.push_back(r);
	}
	json::Value sum(json::array,  header.begin(), header.end(), [&](const std::string_view &x){return json::Value(srp.total[x]);});
	return json::Value(json::object,{
			json::Value("hdr", jhdr),
			json::Value("rows", rows),
			json::Value("sums", sum),
	});
}

void maintenance(DataBase &main, DataBase &backup, TradeReport &rep) {
	if (main.sort_unsorted()) {
		std::cerr << "Main database has been resorted" << std::endl;
		rep.reset();
	}
	if (backup.sort_unsorted()) {
		std::cerr << "Backup database has been resorted" << std::endl;
	}
}

template<typename T, typename U>
class IOSelect {
public:
	IOSelect(bool x, const T &a, const U &b):x(x),a(a),b(b) {}
	template<typename X> friend auto &operator<<(X &&str, const IOSelect &z) {
		if (z.x) return str << z.a; else return str << z.b;
	}
protected:
	bool x; const T &a; const U &b;
};

template<typename T, typename U>
static auto ios_if(bool x, const T &a, const U &b) {
	return IOSelect<T,U>(x,a,b);
}

static void createDump(DataBase &db, bool tocsv) {
	char buff[40];
	if (tocsv) {
		std::cout << "Date,Trader,ID,DEL,Broker,Asset,Currency,Price,Size,Position,Equity change,Realized PnL,INV" << std::endl;
	}
	db.scanTradesFrom(0, [&](off_t ofs, const DataBase::Header &hdr, const DataBase::Trade &trade){

		const DataBase::TraderInfo *tinfo = db.findTrader(hdr);
		if (tinfo) {
			if (tocsv) {

				std::time_t t = hdr.getTime()/1000;
				const std::tm *tm = gmtime(&t);
				std::strftime(buff, sizeof(buff),"%Y-%m-%d %H:%M:%S", tm);
				std::cout << buff << "," <<
							 hdr.uid << "-" <<
							 hdr.magic << "," <<
							 "\""<<trade.getTradeId() << "\"," <<
							 ios_if(trade.deleted,"deleted","") << "," <<
							 tinfo->getBroker() << "," <<
							 tinfo->getAsset() << "," <<
							 tinfo->getCurrency() << "," <<
							 trade.price << "," <<
							 trade.size << "," <<
							 ios_if(std::isfinite(trade.pos),trade.pos,"") << "," <<
							 trade.change << "," <<
							 trade.rpnl << "," <<
							 ios_if(trade.invert_price,"inverted","") << ","
							 "";

			} else {
				json::Value row = json::Object {
					{"time", hdr.getTime()},
					{"uid", hdr.uid},
					{"magic", hdr.magic},
					{"change", trade.change},
					{"pos",std::isnan(trade.pos)?json::Value():json::Value(trade.pos)},
					{"price", trade.price},
					{"invert_price", trade.invert_price},
					{"size", trade.size},
					{"deleted", trade.deleted},
					{"abcpnl", trade.rpnl},
					{"asset", tinfo->getAsset()},
					{"currency", tinfo->getCurrency()},
					{"broker", tinfo->getBroker()}
				};
				json::Value hdr = {"import", row};
				hdr.toStream(std::cout);
			}

			std::cout << std::endl;
		}
		return true;
	});


}

int main(int argc, char **argv) {

	using namespace json;

	if (argc < 2 || argc > 3) {
		std::cerr << "Usage: " << argv[0] << " <path/prefix> [dump|csv]" << std::endl
				  << std::endl
				  << "<path/prefix> Database files path and file prefix. Created if not exists" << std::endl
				  << "dump          Create dump" << std::endl
				  << "csv           Export as CSV" << std::endl;
		return 1;
	}

	std::string path = argv[1];
	std::string path_main = path+".main";
	std::string path_backup = path+".backup";
	std::string path_lock = path+".lock";



	try {

		if (!DataBase::lockFile(path_lock)) {
			std::cerr << "Database file is already opened" << std::endl;
			return 1;
		}


		DataBase db_main(path_main);
		try {
			db_main.buildIndex();
		} catch (const std::exception &e) {
			std::cerr << "Main DB - Warning: " << e.what() << std::endl;
			DataBase db_backup(path_backup);
			db_backup.buildIndex();
			db_main.reconstruct(db_backup);
			std::cerr << "Main DB - Restored" << std::endl;
		}
		std::cerr << "Main DB - OK" << std::endl;

		DataBase db_backup(path_backup);
		try {
			db_backup.buildIndex();
		} catch (const std::exception &e) {
			std::cerr << "Backup DB - Warning: " << e.what() << std::endl;
			db_backup.reconstruct(db_main);
			std::cerr << "Backup DB - Restored" << std::endl;
		}
		std::cerr << "Backup DB - OK" << std::endl;

		if (db_backup.size() < db_main.size()) {
			std::cerr << "Syncing backup DB" << std::endl;
			db_backup.reconstruct(db_main);
			std::cerr << "Done" << std::endl;
		} else if (db_main.size() < db_backup.size()) {
			std::cerr << "Syncing main DB from backup DB" << std::endl;
			db_main.reconstruct(db_backup);
			std::cerr << "Done" << std::endl;
		}


		if (argc == 3) {
			std::string_view cmd = argv[2];
			if (cmd == "dump") createDump(db_main, false);
			else if (cmd == "csv") createDump(db_main, true);
			else throw std::runtime_error("Unknown command");
			return 0;
		}

		TradeReport rpt(db_main);

 		Value req = readFromStream(std::cin);
		Value resp;
		bool runm = false;
		while (req.defined()) {
			runm = true;
			try {
				auto cmd = req[0].getString();
				if (cmd == "sendItem") {
					Value data = req[1];
					storeTrade(db_main, db_backup, rpt, data, true);
					resp = Value(json::array,{true});
					runm = false;

				}else if (cmd == "import") {
					Value data = req[1];
					storeTrade(db_main, db_backup, rpt, data, false);
					resp = Value(json::array,{true});
					runm = false;
				} else if (cmd == "getReport") {
					Value rep = genReport(rpt);
					resp = {true, rep};

				} else {
					throw std::runtime_error("unsupported function");
				}
			} catch (const std::exception &e) {
				resp = {false, e.what()};
			}
			resp.toStream(std::cout);
			std::cout << std::endl;
			if (runm) {
				maintenance(db_main, db_backup, rpt);
			}
			req = readFromStream(std::cin);
		}

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;

}



