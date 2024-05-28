/*
 * database.h
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_RPTBROKER_DATABASE_H_
#define SRC_BROKERS_RPTBROKER_DATABASE_H_

#include <cstdint>
#include <fstream>
#include <map>

namespace json {
	class Value;
}

class DataBase {
public:
	DataBase(const std::string &fname);
	~DataBase();

	static const std::uint16_t recOldTrade = 0;
	static const std::uint16_t recTraderInfo = 1;
	static const std::uint16_t recTrade = 2;


	struct Header {
		std::uint32_t lowtime;  //low part of time (32 bit)
		std::uint16_t hitime;   //high part of time (16 bit)
		std::uint16_t type;     //type of item
		std::uint64_t uid;		//trader's uid
		std::uint64_t magic;	//trader's magic
		bool operator==(const Header &h) const;
		bool operator!=(const Header &h) const {return !operator==(h);}

		void setTime(std::uint64_t tm);
		std::uint64_t getTime() const;
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

	struct TraderInfoExt: public TraderInfo {
		std::uint64_t firstSeen;
		std::uint64_t lastSeen;
		double profit, equity, volume;
		unsigned int trades;
		TraderInfoExt(const TraderInfo &base):TraderInfo(base),firstSeen(-1),lastSeen(0),profit(0),equity(0), volume(0),trades(0) {}
		TraderInfoExt() {}
		void update(std::uint64_t time, double change, double rpnl, double volume);
	};

	struct OldTrade {
		double price;
		double size;
		double change;
		double rpnl;
		bool invert_price;
		bool deleted = false;		//if true, trade is removed from calculations
		char tradeId[50];
		int reserved1 = 0;
		int reserved2 = 0;

		std::string_view getTradeId() const;
	};

	struct Trade {
		char tradeId[50];
		double price;
		double size;
		double pos;
		double change;
		double rpnl;
		bool invert_price;
		bool deleted = false;

		static Trade fromOld(const OldTrade &old);

		void setTradeId(const std::string_view &str);
		std::string_view getTradeId() const;
		double getVolume() const;
};

	struct Payload {
		std::uint16_t type;
		union {
			const OldTrade *old_trade;
			const Trade *trade;
			const TraderInfo *tinfo;
		};
	};

	struct Day {
		int year;
		int month;
		int day;
		struct Cmp {
			bool operator()(const Day &a, const Day &b) const;
		};

		bool operator==(const Day &d) const;
		bool operator!=(const Day &d) const {return !operator==(d);}

		static Day fromTime(std::uint64_t tm);
		Day getMonth() const {return Day{year, month, 0};}

	};



	template<typename Fn> void scanFrom(off_t ofs, Fn &&fn);
	template<typename Fn> void scanTradesFrom(off_t ofs, Fn &&fn);
	void buildIndex();

	std::size_t size() const {return records;}

	const TraderInfoExt *findTrader(const Header &hdr) const;
	TraderInfoExt *findTrader(const Header &hdr);
	off_t findDay(const Day &m) const;


	void addTrade(const Header &hdr, const Trade &trade);
	void addTrader(const Header &hdr,const TraderInfo &trd);
	void replaceTrade(off_t pos, const Header &hdr, const Trade &trade);

	///Reconstruct database from other database, forexample, if corrupted - will clear this db
	void reconstruct(DataBase &db);

	auto days() const {return dayMap;}

	void flush();
	bool sort_unsorted();

	const auto &traders() const {return traderMap;}

	static bool lockFile(const std::string &name);

protected:
	using TraderKey = std::pair<std::uint64_t, std::uint64_t>;
	using TraderMap = std::map<TraderKey, TraderInfoExt>;

	mutable TraderMap traderMap;

	using DayMap = std::map<Day, off_t, Day::Cmp>;
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
	void putTrade(Header hdr, const Trade &trade);
	void putTraderInfo(Header hdr, const TraderInfo &trd);

};

template<typename Fn>
inline void DataBase::scanFrom(off_t pos, Fn &&fn)  {
	setPos(pos);
	Header hdr;
	std::uint64_t chksum;
	Payload p;
	while (read(hdr)) {
		switch(hdr.type) {
		case recOldTrade: {
			unsorted_timestamp = 0;
			if (hdr.getTime() == 0) {//legacy
				TraderInfo nfo;
				read(nfo);read(chksum);
				check_checksum(chksum, hdr, nfo);
				p.type = recTraderInfo;p.tinfo = &nfo;
				if (!fn(pos, hdr, p)) return;
			} else {
				OldTrade trd;
				read(trd);read(chksum);
				check_checksum(chksum, hdr, trd);
				p.type = recOldTrade;p.old_trade = &trd;
				if (!fn(pos, hdr, p)) return;
			}
		}break;
		case recTraderInfo:{
			TraderInfo nfo;
			read(nfo);read(chksum);
			check_checksum(chksum, hdr, nfo);
			p.type = recTraderInfo;p.tinfo = &nfo;
			if (!fn(pos, hdr, p)) return;
		}break;
		case recTrade:{
			Trade nfo;
			read(nfo);read(chksum);
			check_checksum(chksum, hdr, nfo);
			p.type = recTrade;p.trade = &nfo;
			if (!fn(pos, hdr, p)) return;
		}break;
		default: throw std::runtime_error("Database corrupted, unsupported record");
		};
		pos = getPos();
	}

}


template<typename Fn>
inline void DataBase::scanTradesFrom(off_t ofs, Fn &&fn)  {
	scanFrom(ofs, [&](off_t ofs, const Header &hdr, const Payload &payload){
		switch (payload.type) {
		case recTraderInfo:
			traderMap[{hdr.uid,hdr.magic}] = *payload.tinfo;
			return true;
		case recOldTrade:
			return fn(ofs, hdr, Trade::fromOld(*payload.old_trade));
		case recTrade:
			return fn(ofs, hdr, *payload.trade);
		default:
			return true;
		}

	});
}



#endif /* SRC_BROKERS_RPTBROKER_DATABASE_H_ */
