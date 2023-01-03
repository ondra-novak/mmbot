/*
 * httpjson.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include <imtjson/string.h>
#include <imtjson/parser.h>
#include "httpjson.h"

#include <simpleServer/urlencode.h>
#include "../shared/logOutput.h"
#include "log.h"

using ondra_shared::logDebug;
using simpleServer::urlEncode;

HTTPJson::HTTPJson(simpleServer::HttpClient &&httpc,
		const std::string_view &baseUrl)
:httpc(std::move(httpc)),baseUrl(baseUrl)
{
	httpc.setConnectTimeout(5000);
	httpc.setIOTimeout(10000);
}


enum class BodyType {
	none,
	form,
	json
};

static simpleServer::SendHeaders hdrs(const json::Value &headers) {

	simpleServer::SendHeaders hdr;
	for (json::Value v: headers) {
		auto k = v.getKey();
		if (k != "Connection") {
			hdr(k, v.toString().str());
		}

	}
	hdr("Connection","close");
	if (!headers["Accept"].defined()) hdr("Accept","application/json");
	return hdr;
}

json::Value HTTPJson::parseResponse(simpleServer::HttpResponse &resp, json::Value &headers) {
	json::Value r;
	json::Object hh;
	StrViewA ctx = resp.getHeaders()["Content-Type"];
	for (auto &&k: resp.getHeaders()) {
		std::string name;
		std::transform(k.first.begin(), k.first.end(), std::back_inserter(name), tolower);
		hh.set(name, std::string_view(k.second));
	}
	if (force_json || ctx.indexOf("application/json") != ctx.npos) {
		BinaryView b;
		std::size_t pos = 0;
		auto s = resp.getBody();
		r = json::Value::parse([&]{
			if (pos >= b.length) {
				b = s.read();
				if (b.empty()) return -1;
				pos = 0;
				if (reading_fn != nullptr) reading_fn();
			}
			return int(b[pos++]);
		});
	} else {
		std::ostringstream buff;
		auto s = resp.getBody();
		BinaryView b = s.read();
		while (!b.empty()) {
			buff.write(reinterpret_cast<const char*>(b.data), b.length);
			if (reading_fn != nullptr) reading_fn();
			b = s.read();
		}
		r = buff.str();
	}
	headers =hh;
	return r;
}


json::Value HTTPJson::GET(const std::string_view &path, json::Value &&headers, unsigned int expectedCode) {
	std::string url = baseUrl;
	int redir_count = 0;
	url.append(path);

	do {
        
        logDebug("GET $1", url);
    
        auto resp = httpc.request("GET", url, hdrs(headers));
        const simpleServer::ReceivedHeaders &hdrs = resp.getHeaders();
        simpleServer::HeaderValue datehdr = hdrs["Date"];
        if (datehdr.defined()) {
            if (parseHttpDate(datehdr, lastServerTime)) {
                lastLocalTime = std::chrono::steady_clock::now();
            }
        }
        unsigned int st = resp.getStatus();
        if (st == 301 || st == 302 || st == 303 || st == 307) {
            url = handleLocation(url, resp.getHeaders()["Location"]); // @suppress("Invalid arguments") // @suppress("Method cannot be resolved")
            if (!url.empty() && redir_count< 16) {
                redir_count++;
                continue;
            }
        }
        if ((expectedCode && st != expectedCode) || (!expectedCode && st/100 != 2)) {
            throw UnknownStatusException(st, resp.getMessage(),resp);
        }
        json::Value r = parseResponse(resp, headers);
        logDebug("RECV: $1", r);
        return r;
	} while (true);
}

std::string HTTPJson::handleLocation(std::string_view url, simpleServer::HeaderValue loc) {
    if (!loc.defined()) return {};
    std::string_view location = loc;
    auto sep0 = url.find('?');
    if (sep0 != url.npos) {
        url = url.substr(0,sep0);
    }
    if (location.empty()) return std::string(url);
    if (location.find("://") != location.npos) return std::string(location);
    auto sep1 = url.find("://");
    auto sep2 = url.find("/", sep1);
    if (location[0] == '/') {
        if (sep2 == url.npos) return std::string(url).append(location);
        else return std::string(url.substr(0, sep2)).append(location);
    } else {
        return std::string(url).append(location);
    }
}


json::Value HTTPJson::SEND(const std::string_view &path,
		const std::string_view &method, const json::Value &data,
		json::Value &&headers,
		unsigned int expectedCode) {

	std::string url = baseUrl;
	url.append(path);
	auto sdata = data.defined()?data.toString():json::String("");

	if (!headers["Content-Type"].defined()) {
		if (data.type() != json::string ) {
			headers = headers.replace("Content-Type", "application/json");
		} else {
			headers = headers.replace("Content-Type", "application/x-www-form-urlencoded");
		}
	}

	logDebug("$1 $2 - data $3", method, url, data);


	auto resp = httpc.request(method, url, hdrs(headers), sdata.str());
	const simpleServer::ReceivedHeaders &hdrs = resp.getHeaders();
	simpleServer::HeaderValue datehdr = hdrs["Date"];
	if (datehdr.defined()) {
		if (parseHttpDate(datehdr, lastServerTime)) {
			lastLocalTime = std::chrono::steady_clock::now();
		}
	}
	unsigned int st = resp.getStatus();
	if ((expectedCode && st != expectedCode) || (!expectedCode && st/100 != 2)) {
		throw UnknownStatusException(st, resp.getMessage(), resp);
	}
	json::Value r = parseResponse(resp, headers);
	logDebug("RECV: $1", r);
	return r;

}

json::Value HTTPJson::POST(const std::string_view &path,
		const json::Value &data, json::Value &&headers, unsigned int expectedCode) {
	return SEND(path, "POST", data, std::move(headers), expectedCode);
}

json::Value HTTPJson::PUT(const std::string_view &path, const json::Value &data,
		json::Value &&headers, unsigned int expectedCode) {
	return SEND(path, "PUT", data, std::move(headers), expectedCode);
}

json::Value HTTPJson::DELETE(const std::string_view &path,
		const json::Value &data,json::Value &&headers,  unsigned int expectedCode) {
	return SEND(path, "DELETE", data, std::move(headers), expectedCode);
}

void HTTPJson::setBaseUrl(const std::string &url) {
	baseUrl = url;
}

static bool parse_char(std::string_view &date, char c) {
	if (date.empty()) return false;
	char d = date[0];
	if (c != d) return false;
	date = date.substr(1);
	return true;
}

static unsigned long parse_unsigned(std::string_view &date) {
	unsigned long z = 0;
	while (!date.empty()) {
		char c = date[0];
		if (!isdigit(c)) break;
		date = date.substr(1);
		z = z * 10 + (c - '0');
	}
	return z;
}

constexpr unsigned long makeMonth(char a, char b, char c) {
	return static_cast<unsigned long>(a)*65536
		  +static_cast<unsigned long>(b)*256
		  +static_cast<unsigned long>(c);
}

static unsigned long parse_month(std::string_view &date) {
	if (date.length()<3) return 0;
	unsigned long ret;
	switch (makeMonth(date[0],date[1],date[2])) {
		case makeMonth('J', 'a', 'n'): ret=1;break;
		case makeMonth('F', 'e', 'b'): ret=2;break;
		case makeMonth('M', 'a', 'r'): ret=3;break;
		case makeMonth('A', 'p', 'r'): ret=4;break;
		case makeMonth('M', 'a', 'y'): ret=5;break;
		case makeMonth('J', 'u', 'n'): ret=6;break;
		case makeMonth('J', 'u', 'l'): ret=7;break;
		case makeMonth('A', 'u', 'g'): ret=8;break;
		case makeMonth('S', 'e', 'p'): ret=9;break;
		case makeMonth('O', 'c', 't'): ret=10;break;
		case makeMonth('N', 'o', 'v'): ret=11;break;
		case makeMonth('D', 'e', 'c'): ret=12;break;
		default: return 0;
	}

	date = date.substr(3);
	return ret;
}

static bool parse_time(unsigned long &h, unsigned long &m, unsigned long &s, std::string_view &date) {
	h = parse_unsigned(date);
	if (h > 23) return false;
	if (!parse_char(date,':')) return false;
	m = parse_unsigned(date);
	if (m > 59) return false;
	if (!parse_char(date,':')) return false;
	s = parse_unsigned(date);
	if (m > 60) return false;
	return true;
}

static std::chrono::system_clock::time_point timeFromValues(
		unsigned long y, unsigned long m, unsigned long d,
		unsigned long h, unsigned long mm, unsigned long s) {

	struct tm t = {
			static_cast<int>(s),
			static_cast<int>(mm),
			static_cast<int>(h),
			static_cast<int>(d),
			static_cast<int>(m-1),
			static_cast<int>(y-1900)
	};
	return std::chrono::system_clock::from_time_t(timegm(&t));
}


static bool parseRfc1123_date(std::string_view date, std::chrono::system_clock::time_point & tp) {
	auto p = date.find(',');
	if (p == date.npos) return false;
	date = date.substr(p+1);
	if (!parse_char(date,' ')) return false;
	auto day = parse_unsigned(date);
	if (day < 1 || day > 31) return false;
	if (!parse_char(date,' ')) return false;
	auto month = parse_month(date);
	if (month < 1 || month > 12) return false;
	if (!parse_char(date,' ')) return false;
	auto year = parse_unsigned(date);
	if (year < 1000 || month > 9999) return false;
	if (!parse_char(date,' ')) return false;
	unsigned long h,m,s;
	if (!parse_time(h,m,s,date)) return false;
	tp = timeFromValues(year, month, day, h, m, s);
	return true;
}

static bool parseRfc850_date(std::string_view date, std::chrono::system_clock::time_point & tp) {
	auto p = date.find(',');
	if (p == date.npos) return false;
	if (!parse_char(date,' ')) return false;
	auto day = parse_unsigned(date);
	if (day < 1 || day > 31) return false;
	if (!parse_char(date,'-')) return false;
	auto month = parse_month(date);
	if (month < 1 || month > 12) return false;
	if (!parse_char(date,'-')) return false;
	auto year = parse_unsigned(date);
	if (year < 1000 || month > 9999) return false;
	if (!parse_char(date,' ')) return false;
	unsigned long h,m,s;
	if (!parse_time(h,m,s,date)) return false;
	tp = timeFromValues(year, month, day, h, m, s);
	return true;



}


bool HTTPJson::parseHttpDate(const std::string_view &date, std::chrono::system_clock::time_point & tp) {
	return parseRfc1123_date(date, tp) || parseRfc850_date(date, tp);
}

std::chrono::system_clock::time_point HTTPJson::now() {
	if (lastServerTime.time_since_epoch().count() == 0) {
		return std::chrono::system_clock::now();
	} else {
		return lastServerTime + (std::chrono::steady_clock::now() - lastLocalTime);
	}
}
