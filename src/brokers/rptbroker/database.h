/*
 * database.h
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_RPTBROKER_DATABASE_H_
#define SRC_BROKERS_RPTBROKER_DATABASE_H_

#include <fstream>
#include <map>

namespace json {
	class Value;
}


class DataBase {
public:
	DataBase(const std::string &fname);
	~DataBase();

	struct Header {
		std::uint64_t time;  //if time is 0, whole record is RecordDef
		std::uint64_t uid;		//trader's uid
		std::uint64_t magic;	//trader's magic
		bool operator==(const Header &h) const;
		bool operator!=(const Header &h) const {return !operator==(h);}
	};

	struct TraderInfo {
		char broker[100];
		char asset[20];
		char currency[20];

		std::string_view getBroker() const;
		std::string_view getAsset() const;
		std::string_view getCurrency() const;

		void setBroker(const std::string_view &str);
		void setAsset(const std::string_view &str);
		void setCurrency(const std::string_view &str);
	};

	struct Trade {
		double price;
		double size;
		double change;
		double rpnl;
		bool invert_price;
		bool deleted = false;		//if true, trade is removed from calculations
		char tradeId[50];
		int reserved1 = 0;
		int reserved2 = 0;

		void setTradeId(const std::string_view &str);
		std::string_view getTradeId() const;

	};

	struct Month {
		int year;
		int month;
		struct Cmp {
			bool operator()(const Month &a, const Month &b) const;
		};
		bool operator==(const Month &m) const;
		bool operator!=(const Month &m) const {return !operator==(m);}

		static Month fromTime(std::uint64_t tm);
	};

	struct Day: Month {
		int day;
		struct Cmp {
			bool operator()(const Day &a, const Day &b) const;
		};

		bool operator==(const Day &d) const;
		bool operator!=(const Day &d) const {return !operator==(d);}

		static Day fromTime(std::uint64_t tm);

	};

	template<typename Fn>
	void scanFrom(off_t ofs, Fn &&fn);
	void buildIndex();

	std::size_t size() const {return records;}

	const TraderInfo *findTrader(const Header &hdr) const;
	std::streamoff findMonth(const Month &m) const;
	std::streamoff findDay(const Day &m) const;


	void addTrade(const Header &hdr, const Trade &trade);
	void replaceTrade(off_t pos, const Header &hdr, const Trade &trade);
	void addTrader(const Header &hdr,const TraderInfo &trd);

	///Reconstruct database from other database, forexample, if corrupted - will clear this db
	void reconstruct(DataBase &db);

	auto months() const {return monthMap;}
	auto days() const {return dayMap;}

	void flush();
	bool sort_unsorted();

	static bool lockFile(const std::string &name);

protected:
	using TraderKey = std::pair<std::uint64_t, std::uint64_t>;
	using TraderMap = std::map<TraderKey, TraderInfo>;

	mutable TraderMap traderMap;

	using MonthMap = std::map<Month, off_t, Month::Cmp>;
	using DayMap = std::map<Day, off_t, Day::Cmp>;
	MonthMap monthMap;
	DayMap dayMap;

	std::string fname;
	int fd;
	std::size_t records;
	std::streamoff fend;

	template<typename T> void write(const T &data);
	template<typename T> bool read(T &data) ;
	bool read(char *buff, std::size_t sz) ;

	void lockDB();
	off_t getPos() const;
	off_t setPos(off_t pos, int dir);
	off_t setPos(off_t pos);

	char buffer_data[4096];
	std::string_view buffer;
	std::uint64_t last_timestamp, unsorted_timestamp;

	template<typename ... Args> static std::uint64_t checksum(const Args & ... args);
	template<typename ... Args> static void check_checksum(std::uint64_t, const Args & ... args);
};

template<typename Fn>
inline void DataBase::scanFrom(off_t ofs, Fn &&fn)  {
	off_t pos = ofs;
	setPos(pos);
	Header hdr;
	TraderInfo nfo;
	Trade trade;
	std::uint64_t chksum;
	while (read(hdr)) {
		if (hdr.time == 0) {
			read(nfo);
			read(chksum);
			check_checksum(chksum, hdr, nfo);
			traderMap[{hdr.uid,hdr.magic}] = nfo;
		} else {
			read(trade);
			read(chksum);
			check_checksum(chksum, hdr, trade);
			if (!fn(pos, hdr, trade)) break;
		}
		pos = getPos();
	}
}

#endif /* SRC_BROKERS_RPTBROKER_DATABASE_H_ */
