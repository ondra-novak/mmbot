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
	monthMap.clear();
	dayMap.clear();
	scanFrom(0, [&](off_t pos, const Header &hdr, const Trade &trd){
		if (hdr.time < last_timestamp) {
			unsorted_timestamp = std::min(unsorted_timestamp, hdr.time);
		} else {
			last_timestamp = hdr.time;
		}
		if (findTrader(hdr) == nullptr) throw std::runtime_error("Build index failed: Database is corrupted");
		Day d = Day::fromTime(hdr.time);
		if (d != last) {
			last = d;
			monthMap.emplace(d, pos);
			dayMap.emplace(d, pos);
		}
		records++;
		return true;
	});
	fend = getPos();

}

bool DataBase::Month::Cmp::operator ()(const Month &a, const Month &b) const {
	if (a.year != b.year) return a.year < b.year;
	else return a.month < b.month;
}

const DataBase::TraderInfo *DataBase::findTrader(const Header &hdr) const {
	auto iter = traderMap.find({hdr.uid, hdr.magic});
	if (iter == traderMap.end()) return nullptr;
	else return &iter->second;
}

std::streamoff DataBase::findMonth(const Month &m) const {
	std::streamoff pos = fend;
	auto iter = monthMap.lower_bound(m);
	if (iter != monthMap.end()) pos = iter->second;
	return pos;
}

std::streamoff DataBase::findDay(const Day &m) const {
	std::streamoff pos = fend;
	auto iter = dayMap.lower_bound(m);
	if (iter != dayMap.end()) pos = iter->second;
	return pos;
}

bool DataBase::Month::operator ==(const Month &m) const {
	return this->month == m.month && this->year == m.year;
}

void DataBase::addTrade(const Header &hdr, const Trade &trade) {
	if (hdr.time == 0) throw std::runtime_error("Invalid time in header");
	if (hdr.time <= last_timestamp) {
		unsorted_timestamp = std::min(unsorted_timestamp, hdr.time);
	} else {
		last_timestamp = hdr.time;
	}
	fend = setPos(0, SEEK_END);
	Day d = Day::fromTime(hdr.time);
	monthMap.emplace(d, fend); //will not overwrite existing record
	dayMap.emplace(d, fend); //will not overwrite existing record
	write(hdr);
	write(trade);
	write(checksum(hdr,trade));
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
	if (hdr.time != 0) {
		Header h = hdr;
		h.time = 0;
		addTrader(h, trd);
	} else {
		setPos(0,SEEK_END);
		write(hdr);
		write(trd);
		write(checksum(hdr,trd));
		fend = getPos();
		traderMap.emplace(TraderKey{hdr.uid, hdr.magic},trd);
	}

}

DataBase::Month  DataBase::Month::fromTime(std::uint64_t tm) {
	std::time_t t = tm/1000;
	const std::tm *tinfo = std::gmtime(&t);
	return { tinfo->tm_year,tinfo->tm_mon+1};
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
	db.scanFrom(0, [&](off_t, const Header &hdr, const Trade &trd) {

		auto tinfo = findTrader(hdr);
		if (tinfo == nullptr) {
			const TraderInfo *otherTrader = db.findTrader(hdr);
			if (otherTrader == nullptr) throw std::runtime_error("Can't reconstruct, source database is corrupted");
			addTrader(hdr, *otherTrader);
		}
		addTrade(hdr, trd);
		return true;
	});
}

bool DataBase::Header::operator ==(const Header &h) const {
	return time == h.time && magic == h.magic && uid == h.uid;
}

bool DataBase::Day::Cmp::operator ()(const Day &a, const Day &b) const {
	if (a.Month::operator ==(b)) return a.day < b.day;
	else return Month::Cmp()(a,b);
}

bool DataBase::Day::operator ==(const Day &d) const {
	return Month::operator==(d) && day == d.day;
}

DataBase::Day DataBase::Day::fromTime(std::uint64_t tm) {
	std::time_t t = tm/1000;
	const std::tm *tinfo = std::gmtime(&t);
	return { tinfo->tm_year,tinfo->tm_mon+1, tinfo->tm_mday};

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
	auto offs = findDay(d);
	setPos(offs, SEEK_SET);
	Header h;
	TraderInfo tinfo;
	Trade tr;
	std::uint64_t chksum;
	while (read(h)) {
		if (h.time == 0) {
			read(tinfo);
			read(chksum);
			check_checksum(chksum, h, tinfo);
			traders.emplace_back(h,tinfo);
		} else {
			read(tr);
			read(chksum);
			check_checksum(chksum, h, tr);
			trades.emplace_back(h,tr);
		}
	}
	std::sort(trades.begin(), trades.end(), [&](const auto &a, const auto &b) {
		return a.first.time < b.first.time;
	});
	setPos(offs, SEEK_SET);
	for (const auto &x: traders) {
		write(x.first);
		write(x.second);
		write(checksum(x.first, x.second));
	}
	for (const auto &x: trades) {
		write(x.first);
		write(x.second);
		write(checksum(x.first, x.second));
	}
	if (ftruncate(fd, getPos())) {
		throw std::system_error(errno, std::system_category());
	}
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
template bool DataBase::read(DataBase::Trade &);
template bool DataBase::read(std::uint64_t &);
template void DataBase::check_checksum(std::uint64_t, const DataBase::Header &, const DataBase::TraderInfo &);
template void DataBase::check_checksum(std::uint64_t, const DataBase::Header&,
		const DataBase::Trade&);

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
