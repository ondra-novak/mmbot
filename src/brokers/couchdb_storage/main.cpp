/*
 * main.cpp
 *
 *  Created on: 29. 5. 2021
 *      Author: ondra
 */


#include <ctime>
#include <shared/default_app.h>
#include <imtjson/binary.h>
#include <imtjson/string.h>
#include <imtjson/binjson.tcc>
#include <imtjson/serializer.h>
#include <simpleServer/urlencode.h>
#include "../api.h"
#include "../httpjson.h"


static ondra_shared::DefaultApp app({},std::cerr);
static std::string_view cfg_prefix = "cfg.";
static std::string_view trade_prefix = "t.";
static json::Value design_document = json::Value(json::object,{
	json::Value("_id","_design/report"),
	json::Value("language","javascript"),
	json::Value("views",json::Value(json::object,{
		json::Value("month", json::Value(json::object,{
			json::Value("map",R"js(
function(doc) {
   if (doc._id[0] == 't') {
	  var d = new Date(doc.time);
	  var m = d.getYear()*12+d.getMonth();
	  if (typeof doc.change == "number") emit([doc.ident,m,doc.currency],doc.change);
   }
}
			)js"),
			json::Value("reduce","_sum")
		})),
		json::Value("day", json::Value(json::object,{
			json::Value("map",R"js(
function(doc) {
   if (doc._id[0] == 't') {
	  var m = Math.floor(doc.time/(24*60*60*1000));
	  if (typeof doc.change == "number") emit([doc.ident,m,doc.currency],doc.change);
   }
}
			)js"),
			json::Value("reduce","_sum")
		}))

	})),




});

class DBConn {
public:

	DBConn(const std::string_view &url, const std::string_view &login, const std::string_view &password, const std::string_view &ident);

	void put(const std::string_view &name, const json::Value &data);
	json::Value get(const std::string_view &name);
	void erase(const std::string_view &name);

	void put_trade(const json::Value &trade);
	void flush_trades();
	json::Value get_report(bool rep = true);



protected:
	HTTPJson api;
	json::Value auth;
	json::Value rev;
	std::string ident;
	std::vector<json::Value> trades;

	std::string buildPath(const std::string_view &name) const;
	void updateRev();
};

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



int main(int argc, char **argv) {

	using namespace json;

	if (!app.init(argc, argv)) {
		std::cerr << "Invalid arguments" << std::endl;
	}

	auto database = app.config["database"];
	auto ident = app.config["ident"];
	DBConn db(
		database.mandatory["url"].getString(),
		database.mandatory["login"].getString(),
		database.mandatory["password"].getString(),
		ident.mandatory["ident"].getString()
	);

	try {
 		Value req = readFromStream(std::cin);
		Value resp;
		while (req.defined()) {
			try {
				auto cmd = req[0].getString();
				if (cmd == "store") {
					Value data = req[1];
					Value name = data[0];
					Value content = data[1];
					db.put(name.getString(), content);
					resp = Value(json::array,{true});
				} else if (cmd == "load") {
					Value name = req[1];
					Value r = db.get(name.getString());
					resp = {true, r};
				} else if (cmd == "erase") {
					Value name = req[1];
					db.erase(name.getString());
					resp = Value(json::array,{true});
				} else if (cmd == "sendItem") {
					Value data = req[1];
					db.put_trade(data);
					resp = Value(json::array,{true});
				} else if (cmd == "getReport") {
					Value rep = db.get_report();
					resp = {true, rep};

				} else {
					throw std::runtime_error("unsupported function");
				}
			} catch (const std::exception &e) {
				resp = {false, e.what()};
			}
			resp.toStream(std::cout);
			std::cout << std::endl;
			db.flush_trades();
			req = readFromStream(std::cin);
		}

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;

}

static json::Value createAuthStr(const std::string_view &username, const std::string_view &password) {
	std::string buff;
	std::string res;
	buff.reserve(username.length()+1+password.length());
	buff.append(username);
	buff.push_back(':');
	buff.append(password);
	res.reserve((buff.size()+4)*4/3);
	json::base64->encodeBinaryValue(
			json::BinaryView(reinterpret_cast<const unsigned char *>(buff.data()),buff.length()),
			[&](const json::StrViewA &txt){
		res.append(txt.data, txt.length);
	});
	return json::Value(json::object, {
			json::Value("Authorization", json::String({"Basic ",res}))
	});
}


inline DBConn::DBConn(const std::string_view &url,
		const std::string_view &login, const std::string_view &password,
		const std::string_view &ident)
:api(simpleServer::HttpClient("", simpleServer::newHttpsProvider(), 0, 0), url)
,auth(createAuthStr(login, password))
,ident(ident)
{
	updateRev();
}

inline void DBConn::put(const std::string_view &name, const json::Value &data) {
	try {
		json::Value res = api.PUT(buildPath(name), data, json::Value(auth));
		rev = res["rev"];
	} catch (HTTPJson::UnknownStatusException &e) {
		if (e.getStatusCode() == 409) {
			updateRev();
			json::Value res = api.PUT(buildPath(name), data, json::Value(auth));
			rev = res["rev"];
			return;
		}
		throw;
	}
}

inline json::Value DBConn::get(const std::string_view &name) {
		json::Value res = api.GET(buildPath(name), json::Value(auth));
		return res;
}

inline void DBConn::erase(const std::string_view &name) {
	using namespace json;
	Value res = api.DELETE(buildPath(name), Value(), Value(auth));
	rev = res["rev"];
}

inline std::string DBConn::buildPath(const std::string_view &name) const {
	std::string out;
	out.reserve(100);
	out.push_back('/');
	auto fn = [&](char c) {
		out.push_back(c);
	};
	out.append(cfg_prefix);
	simpleServer::UrlEncode<decltype(fn)> encoder(fn);
	for (char c: ident) encoder(c);
	out.push_back('/');
	if (name.empty()) out.append("empty_empty");
	else if (name[0] == '_') out.push_back('X');
	for (char c: name) encoder(c);
	if (rev.defined()) {
		out.append("?rev=");
		out.append(rev.getString());
	}
	return out;
}

inline void DBConn::put_trade(const json::Value &trade) {
	using namespace json;
	std::string id;
	std::string buff;
	Value identData={ident,trade["uid"],trade["tradeId"]};
	identData.serializeBinary([&](char c){
		buff.push_back(c);
	});
	id = trade_prefix;
	base64url->encodeBinaryValue(BinaryView(StrViewA(buff)),[&](StrViewA str){
		id.append(str.data, str.length);
	});
	Value doc = trade.replace("_id", id).replace("ident", ident);
	trades.push_back(doc);
}

inline void DBConn::flush_trades() {
	if (trades.empty()) return;
	using namespace json;
	using namespace ondra_shared;
	Value bulk (object,{
			Value("docs",Value(array,trades.begin(), trades.end(),[](Value x){return x;}))
	});
	try {
		api.POST("/_bulk_docs", bulk, Value(auth));
		trades.clear();
	} catch (std::exception &e) {
		logError("Error store trades: $1", e.what());
	}
}

inline json::Value DBConn::get_report(bool rep) {
	using namespace json;
	try {
		auto now = std::chrono::system_clock::now();
		auto timestamp = std::chrono::system_clock::to_time_t(now);
		struct tm tmstruct;
		gmtime_r(&timestamp, &tmstruct);
		auto month_id = tmstruct.tm_year*12+tmstruct.tm_mon;
		tmstruct.tm_mday = 1;
		tmstruct.tm_hour = 0;
		tmstruct.tm_min = 0;
		tmstruct.tm_sec = 0;
		time_t from_tm = timegm(&tmstruct);

		Value month_date_from = {ident, 0};
		Value month_date_to = {ident, month_id};
		Value day_date_from = {ident, from_tm/(24*60*60)};
		Value day_date_to = {ident, ""};

		std::string uri;
		auto srl_to_url=[&](char c){uri.push_back(c);};
		simpleServer::UrlEncode<decltype(srl_to_url)> enc(srl_to_url);

		uri.reserve(200);
		uri.append("/_design/report/_view/month?group_level=3");
		uri.append("&exclude_end=false");
		uri.append("&start_key=");
		month_date_from.serialize(enc);
		uri.append("&end_key=");
		month_date_to.serialize(enc);

		Value month_data = api.GET(uri, Value(auth))["rows"];

		uri.clear();
		uri.append("/_design/report/_view/day?group_level=3");
		uri.append("&start_key=");
		day_date_from.serialize(enc);
		uri.append("&end_key=");
		day_date_to.serialize(enc);

		Value day_data = api.GET(uri, Value(auth))["rows"];

		std::unordered_map<json::Value, json::Value> row;
		std::unordered_map<json::Value, double> sums;
		for(Value x: month_data) {
			Value k = x["key"][2];
			row[k] = 0;
			sums[k] = 0;
		}
		for(Value x: day_data) {
			Value k = x["key"][2];
			row[k] = 0;
			sums[k] = 0;
		}

		Value hdrs (array,row.begin(), row.end(),[](const auto &x){
			return x.first;
		});
		hdrs.unshift("Date");

		std::vector<json::Value> outdata;

		auto append_row = [&](auto &x){
			Value res = x.second;
			x.second = 0;
			sums[x.first]+=res.getNumber();
			return res;
		};

		auto build_rows=[&](Value data, auto &&tmfn) {
			std::uint64_t ln = 0;
			for (Value x: data) {
				auto m = x["key"][1].getUInt();
				if (ln != m) {
					if (ln) {
						Value r(array, row.begin(), row.end(), append_row);
						r.unshift(tmfn(ln));
						outdata.push_back(r);
					}
				}
				Value k = x["key"][2];
				Value v = x["value"];
				row[k] = v;
				ln = m;
			}
			if (ln) {
				Value r(array, row.begin(), row.end(), append_row);
				r.unshift(tmfn(ln));
				outdata.push_back(r);
			}
		};

		build_rows(month_data,[](std::uint64_t ln){
			struct tm t = {};
			t.tm_year = ln/12;
			t.tm_mon = ln%12;
			return timegm(&t);
		});
		build_rows(day_data,[](std::uint64_t ln){
			return ln*24*60*60;
		});
		Value jrows(array,outdata.begin(),outdata.end(),[](Value x){return x;});
		Value jsums(array,sums.begin(),sums.end(),[](auto &x){return x.second;});
		return Value(object,{
				Value("hdr", hdrs),
				Value("rows", jrows),
				Value("sums", jsums),
		});
	} catch (HTTPJson::UnknownStatusException &e) {
		if (e.getStatusCode() == 404) {
			try {
				api.POST("/", design_document, Value(auth));
				if (rep) return get_report(false);
				else throw;
			} catch (HTTPJson::UnknownStatusException &e) {
				if (e.getStatusCode() == 403) {
					return Value(object,{
							Value("hdrs", {"Date","Error"}),
							Value("rows", {std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),"Database account need temporary admin level to initialize database"}),
							Value("sums", {0})
					});
				}
				throw;
			}
		}
		throw;
	}
};

inline void DBConn::updateRev() {
	using namespace json;
	String doc({cfg_prefix,ident});
	Value v = api.POST("/_all_docs", Value(object,{
			Value("keys",Value(array,{Value(doc)}))}), Value(auth), 200);
	rev = v["rows"][0]["value"]["rev"];
}
