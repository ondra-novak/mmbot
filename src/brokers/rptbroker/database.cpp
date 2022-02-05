/*
 * database.cpp
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#include <unistd.h>
#include <ctime>
#include <ext/stdio_filebuf.h>
#include "database.h"

#include <algorithm>
#include <vector>
extern "C" {
#include <errno.h>
#include <sys/file.h>  // for flock()
}


DataBase::DataBase(const std::string &fname)
:fname(fname) {
	fd = ::open(fname.c_str(), O_RDWR| O_CREAT|O_CLOEXEC, 0666);
	if (fd<0) throw std::system_error(errno, std::system_category());
	last_timestamp = 0;
	unsorted_timestamp = -1;
}

DataBase::~DataBase() {
	::close(fd);
}

void DataBase::buildIndex() {
	records = 0;
	Day last{0,0,0};
	last_timestamp = 0;
	unsorted_timestamp = -1;
	dayMap.clear();
	scanTradesFrom(0, [&](off_t pos, const Header &hdr, const Trade &trd){
		auto hdr_time = hdr.getTime();
		if ( hdr_time < last_timestamp) {
			unsorted_timestamp = std::min(unsorted_timestamp, hdr_time);
		} else {
			last_timestamp = hdr_time;
		}
		if (findTrader(hdr) == nullptr) throw std::runtime_error("Build index failed: Database is corrupted");
		Day d = Day::fromTime(hdr_time);
		if (d != last) {
			last = d;
			dayMap.emplace(d, pos);
		}
		records++;
		return true;
	});
	fend = getPos();

}


const DataBase::TraderInfo *DataBase::findTrader(const Header &hdr) const {
	auto iter = traderMap.find({hdr.uid, hdr.magic});
	if (iter == traderMap.end()) return nullptr;
	else return &iter->second;
}


off_t DataBase::findDay(const Day &m) const {
	off_t pos = fend;
	auto iter = dayMap.lower_bound(m);
	if (iter != dayMap.end()) pos = iter->second;
	return pos;
}

void DataBase::putTrade(Header hdr, const Trade &trade) {
	hdr.type = recTrade;
	write(hdr);
	write(trade);
	write(checksum(hdr,trade));

}

void DataBase::addTrade(const Header &hdr, const Trade &trade) {
	auto hdr_time = hdr.getTime();
	if (hdr_time == 0) throw std::runtime_error("Invalid time in header");
	if (hdr_time <= last_timestamp) {
		unsorted_timestamp = std::min(unsorted_timestamp, hdr_time);
	} else {
		last_timestamp = hdr_time;
	}
	fend = setPos(0, SEEK_END);
	Day d = Day::fromTime(hdr_time);
	dayMap.emplace(d, fend); //will not overwrite existing record
	putTrade(hdr, trade);
	fend = getPos();
}

void DataBase::replaceTrade(off_t pos, const Header &hdr, const Trade &trade) {
	Header h;
	setPos(pos, SEEK_SET);
	if (!read(h) || h != hdr) {
		throw std::runtime_error("Invalid trade ID");
	}
	write(trade);
	write(checksum(hdr,trade));
}

template<typename T>
void DataBase::write(const T &data) {
	if (!buffer.empty()) {
		::lseek(fd, -buffer.size(), SEEK_CUR);
		buffer = std::string_view();
	}
	auto s = ::write(fd, &data, sizeof(data));
	if (s < 0) {
		auto e = errno;
		throw std::system_error(e, std::system_category());
	} else if (s != sizeof(data)) {
		throw std::runtime_error("No space on device");
	}
}

bool DataBase::read(char *buff, std::size_t sz) {
	if (!buffer.empty()) {
		auto sub = buffer.substr(0,sz);
		auto ln = sub.size();
		std::copy(sub.begin(), sub.end(), buff);
		buffer = buffer.substr(ln);
		if (ln < sz) {
			if (read(buff+ln, sz-ln)) {
				return true;
			} else {
				throw std::runtime_error( "Unexpected end of file");
			}
		} else {
			return true;
		}
	} else {
		auto s = ::read(fd,buffer_data, sizeof(buffer_data));
		if (s < 0) {
			throw std::system_error(errno, std::system_category(), "Can't read database file");
		}
		else if (s == 0) {
			return false;
		} else {
			buffer = std::string_view(buffer_data, s);
			return read(buff,sz);
		}
	}
}
template<typename T>
bool DataBase::read(T &data) {
	return read(reinterpret_cast<char *>(&data), sizeof(data));
}

void DataBase::addTrader(const Header &hdr, const TraderInfo &trd) {
	setPos(0,SEEK_END);
	putTraderInfo(hdr, trd);
	fend = getPos();

}

void DataBase::putTraderInfo(Header hdr, const TraderInfo &trd) {
	hdr.type = recTraderInfo;
	write(hdr);
	write(trd);
	write(checksum(hdr,trd));
	traderMap[TraderKey{hdr.uid, hdr.magic}] = trd;

}


template<typename T>
std::string_view legacy2strview(T &src) {
	int count = 0;
	for (char c: src) {
		if (c) count++;
		else break;
	}
	return std::string_view(src, count);
}

template<typename T>
void fillLegacy(T &src, const std::string_view text) {
	auto iter = text.begin();
	for (char &c: src) {
		if (iter == text.end()) c = 0;
		else {
			c = *iter;
			++iter;
		}
	}
}

std::string_view DataBase::OldTrade::getTradeId() const{
	return legacy2strview(tradeId);
}
std::string_view DataBase::Trade::getTradeId() const{
	return legacy2strview(tradeId);
}
std::string_view DataBase::TraderInfo::getBroker() const{
	return legacy2strview(broker);
}
std::string_view DataBase::TraderInfo::getAsset() const{
	return legacy2strview(asset);
}
std::string_view DataBase::TraderInfo::getCurrency() const{
	return legacy2strview(currency);
}

void DataBase::Trade::setTradeId(const std::string_view &str){
	fillLegacy(tradeId,str);
}
void DataBase::TraderInfo::setBroker(const std::string_view &str){
	fillLegacy(broker,str);
}
void DataBase::TraderInfo::setAsset(const std::string_view &str){
	fillLegacy(asset,str);
}
void DataBase::TraderInfo::setCurrency(const std::string_view &str){
	fillLegacy(currency,str);
}

void DataBase::reconstruct( DataBase &db) {
	setPos(0, SEEK_SET);
	if (ftruncate(fd, 0)<0) throw std::system_error(errno, std::system_category());
	db.scanTradesFrom(0, [&](off_t, const Header &hdr, const Trade &trd) {

		auto tinfo = findTrader(hdr);
		if (tinfo == nullptr) {
			const TraderInfo *otherTrader = db.findTrader(hdr);
			if (otherTrader == nullptr) throw std::runtime_error("Can't reconstruct, source database is corrupted");
			addTrader(hdr, *otherTrader);
		}
		addTrade(hdr, trd);
		return true;
	});
	buildIndex();
}

bool DataBase::Header::operator ==(const Header &h) const {
	return magic == h.magic && uid == h.uid && lowtime == h.lowtime && hitime == h.hitime && type == h.type;
}

bool DataBase::Day::Cmp::operator ()(const Day &a, const Day &b) const {
	if (a.year != b.year) return a.year < b.year;
	if (a.month != b.month) return a.month < b.month;
	return a.day < b.day;
}

bool DataBase::Day::operator ==(const Day &d) const {
	return year == d.year && month == d.month && day == d.day;
}

DataBase::Day DataBase::Day::fromTime(std::uint64_t tm) {
	std::time_t t = tm/1000;
	const std::tm *tinfo = std::gmtime(&t);
	return { tinfo->tm_year+1900,tinfo->tm_mon+1, tinfo->tm_mday};

}

void DataBase::flush() {
	fdatasync(fd);
}

off_t DataBase::getPos() const {
	auto p = lseek(fd,0,SEEK_CUR);
	if (p < 0) throw std::system_error(errno, std::system_category());
	return p-buffer.size();

}

bool DataBase::sort_unsorted() {
	if (unsorted_timestamp == static_cast<std::uint64_t>(-1)) return false;
	Day d = Day::fromTime(unsorted_timestamp);
	std::vector<std::pair<Header, TraderInfo> > traders;
	std::vector<std::pair<Header, Trade> > trades;
	auto offs = unsorted_timestamp?findDay(d):0;
	scanFrom(offs, [&](off_t, const Header &hdr, const Payload &pl){
		switch (pl.type) {
		case recOldTrade: trades.emplace_back(hdr, Trade::fromOld(*pl.old_trade)) ;break;
		case recTraderInfo: traders.emplace_back(hdr, *pl.tinfo);break;
		case recTrade:trades.emplace_back(hdr, *pl.trade) ;break;
		}
		return true;
	});
	std::sort(trades.begin(), trades.end(), [&](const auto &a, const auto &b) {
		return a.first.getTime() < b.first.getTime();
	});
	setPos(offs, SEEK_SET);
	for (const auto &x: traders) putTraderInfo(x.first, x.second);
	for (const auto &x: trades)  putTrade(x.first, x.second);
	if (ftruncate(fd, getPos())) {
		throw std::system_error(errno, std::system_category());
	}
	flush();
	buildIndex();
	return true;
}

off_t DataBase::setPos(off_t pos, int dir) {
	buffer = std::string_view();
	auto p = lseek(fd, pos, dir);
	if (p < 0) throw std::system_error(errno, std::system_category());
	return p;
}

std::uint64_t checksum() {return 0;}

template<typename T, typename ... Args>
std::uint64_t checksum(const T &x,  const Args & ... args) {
	std::hash<std::string_view> h;
	std::uint64_t c = h(std::string_view(reinterpret_cast<const char *>(&x), sizeof(x)));
	return c + checksum(args...);
}

template<typename ... Args> std::uint64_t DataBase::checksum(const Args & ... args) {
	return ::checksum(args...);
}

template<typename ... Args> void DataBase::check_checksum(std::uint64_t chk, const Args & ... args) {
	if (chk != checksum(args...)) throw std::runtime_error("Database corrupted - checksum failed");
}

template bool DataBase::read(DataBase::Header &);
template bool DataBase::read(DataBase::TraderInfo &);
template bool DataBase::read(DataBase::OldTrade &);
template bool DataBase::read(std::uint64_t &);
template bool DataBase::read(DataBase::Trade&);
template void DataBase::check_checksum(std::uint64_t, const DataBase::Header &, const DataBase::TraderInfo &);
template void DataBase::check_checksum(std::uint64_t, const DataBase::Header&, const DataBase::OldTrade&);
template void DataBase::check_checksum(std::uint64_t, const DataBase::Header&, const DataBase::Trade &);

		off_t DataBase::setPos(off_t pos) {
	return setPos(pos, SEEK_SET);
}

bool DataBase::lockFile(const std::string &name) {
	int fd = ::open(name.c_str(), O_RDWR| O_CREAT, 0666);
	if (fd < 0) throw std::system_error(errno, std::system_category());
	int x= flock(fd, LOCK_EX|LOCK_NB);
	if (x < 0) {
		x = errno;
		if (x == EWOULDBLOCK) {
			::close(fd);
			return false;
		} else {
			throw std::system_error(x, std::system_category());
		}
	}
	return true;
}

void DataBase::Header::setTime(std::uint64_t tm) {
	lowtime = static_cast<std::uint32_t>(tm & 0xFFFFFFFFL);
	hitime = static_cast<std::uint16_t>(tm >> 32);
}

std::uint64_t DataBase::Header::getTime() const {
	return (static_cast<std::uint64_t>(hitime) << 32) | lowtime;

}

DataBase::Trade DataBase::Trade::fromOld(const OldTrade &old) {
	Trade x;
	x.setTradeId(old.getTradeId());
	x.price = old.price;
	x.size = old.size;
	x.pos = std::numeric_limits<double>::quiet_NaN();
	x.change = old.change;
	x.rpnl = old.rpnl;
	x.invert_price = old.invert_price;
	x.deleted = old.deleted;
	return x;
}
