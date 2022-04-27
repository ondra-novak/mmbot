/*
 * httpjson.cpp
 *
 *  Created on: 15. 11. 2019
 *      Author: ondra
 */

#include <imtjson/string.h>
#include <imtjson/parser.h>
#include <imtjson/serializer.h>
#include "httpjson.h"

#include "../shared/logOutput.h"
#include <userver/ssl.h>

#include "log.h"

using ondra_shared::logDebug;

HTTPJson::HTTPJson(const std::string_view &baseUrl)
:httpc({
	"Mozilla/5.0 (compatible; MMBot/3.0; +https://github.com/ondra-novak/mmbot)",
	10000,10000,
	nullptr,
	userver::sslConnectFn(),
	nullptr
}),baseUrl(baseUrl),baseUrlSz(baseUrl.size()){

}


enum class BodyType {
	none,
	form,
	json
};

class Hdrs {
public:
	Hdrs(const json::Value &v) {
		lines.reserve(v.size()+2);
		json::Object repl(v);
		for (json::Value x: v) {
			if ((x.type() != json::string) || (x.flags() & json::binaryString)) {
				repl.set(x.getKey(), x.toString());
			}
		}
		hld = repl;
		bool add_accept = true;
		for (json::Value x: hld) {
			if (!userver::HeaderValue::iequal(x.getKey(),"connection")) {
				lines.push_back({
					x.getKey(), x.getString()
				});
			}
			if (userver::HeaderValue::iequal(x.getKey(),"accept")) add_accept = false;
		}
		lines.push_back({"Connection","close"});
		if (add_accept) {
			lines.push_back({"Accept","application/json"});
		}
	}
	operator userver::HttpClient::HeaderList() const {
		return userver::HttpClient::HeaderList(lines.data(), lines.size());
	}

protected:
	std::vector<userver::HttpClient::HeaderPair> lines;
	json::Value hld;

};

json::Value HTTPJson::parseResponse(userver::PHttpClientRequest &resp, json::Value &headers) {
	json::Value r;
	json::Object hh;
	auto ctx = resp->get("Content-Type");
	for (auto &&k: *resp) {
		std::string name;
		std::transform(k.first.begin(), k.first.end(), std::back_inserter(name), tolower);
		hh.set(name, std::string_view(k.second));
	}
	if (force_json || ctx.find("application/json") != ctx.npos) {
		auto &s = resp->getResponse();
		r = json::Value::parse([&]{
			return s.getChar();
		});
	} else {
		buffer.clear();
		auto &s = resp->getResponse();
		int i=s.getChar();
		while(i>=0){
			buffer.push_back(static_cast<char>(i));
			i=s.getChar();
		}

		r = std::string_view(buffer.data(),buffer.size());
	}
	headers =hh;
	return r;
}

json::Value HTTPJson::GET(const std::string_view &path, json::Value &&headers, unsigned int expectedCode) {
	return GETq(path,json::Value(),std::move(headers), expectedCode);
}


json::Value HTTPJson::GETq(const std::string_view &path,const json::Value &query, json::Value &&headers, unsigned int expectedCode) {
	baseUrl.resize(baseUrlSz);
	baseUrl.append(path);
	buildQuery(query, baseUrl, "?");

	logDebug("GET $1", baseUrl);

	userver::PHttpClientRequest resp(httpc.GET(baseUrl,Hdrs(headers)));
	if (resp == nullptr) throw std::runtime_error("Failed to connect service");

	auto datehdr =resp->get("Date");
	if (datehdr.defined) {
		if (parseHttpDate(datehdr, lastServerTime)) {
			lastLocalTime = std::chrono::steady_clock::now();
		}
	}

	unsigned int st = resp->getStatus();
	if ((expectedCode && st == expectedCode) || (!expectedCode && st/100==2)) {
		json::Value r = parseResponse(resp, headers);
		logDebug("RECV: $1", r);
		return r;
	} else {
		json::Value err;
		try {
			err = parseResponse(resp, headers);
		} catch (...) {

		}
		throw UnknownStatusException(st, std::string(resp->getStatusMessage()),err, headers);
	}
}


json::Value HTTPJson::SEND(const std::string_view &path,
		const std::string_view &method, const json::Value &data,
		json::Value &&headers,
		unsigned int expectedCode) {

	baseUrl.resize(baseUrlSz);
	baseUrl.append(path);

	auto sdata = data.defined()?data.toString():json::String("");

	if (!headers["Content-Type"].defined()) {
		if (data.type() != json::string ) {
			headers = headers.replace("Content-Type", "application/json");
		} else {
			headers = headers.replace("Content-Type", "application/x-www-form-urlencoded");
		}
	}

	logDebug("$1 $2 - data $3", method, baseUrl, data);

	userver::PHttpClientRequest resp = httpc.sendRequest(method, baseUrl, Hdrs(headers), sdata.str());
	if (resp == nullptr) throw std::runtime_error("Failed to connect service");

	userver::HeaderValue datehdr = resp->get("Date");;
	if (datehdr.defined) {
		if (parseHttpDate(datehdr, lastServerTime)) {
			lastLocalTime = std::chrono::steady_clock::now();
		}
	}
	unsigned int st = resp->getStatus();
	if ((expectedCode && st != expectedCode) || (!expectedCode && st/100 != 2)) {
		json::Value err;
		try {
			err = parseResponse(resp, headers);
		} catch (...) {

		}
		throw UnknownStatusException(st, std::string(resp->getStatusMessage()),err,headers);
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
	baseUrlSz = baseUrl.size();
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

const char* HTTPJson::UnknownStatusException::what() const noexcept {
	if (whatMsg.empty()) {
		whatMsg = std::to_string(code);
		whatMsg.append(" ");
		whatMsg.append(message);
		if (body.defined()) {
			whatMsg.append(" ");
			body.serialize([&](char c){whatMsg.push_back(c);});
		}
	}
	return whatMsg.c_str();
}

void HTTPJson::buildQuery(const json::Value items, std::string &out, std::string_view sep) {
	for (json::Value field : items) {
		out.append(sep);
		out.append(field.getKey());
		out.push_back('=');
		json::String s = field.toString();
		if (!s.empty()) {
			json::urlEncoding->encodeBinaryValue(json::map_str2bin(s), [&](std::string_view x){
				out.append(x);
			});
		}
		sep = "&";
	}
}


std::string HTTPJson::urlEncode(std::string_view x) {
	std::string out;
	json::urlEncoding->encodeBinaryValue(json::map_str2bin(x),[&](std::string_view x){
		out.append(x);
	});
	return out;
}
