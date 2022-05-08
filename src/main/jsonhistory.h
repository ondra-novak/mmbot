/*
 * json_history.h
 *
 *  Created on: 8. 5. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_JSONHISTORY_H_
#define SRC_MAIN_JSONHISTORY_H_
#include <imtjson/value.h>
#include "jsondiff.h"

class JSONHistory {
public:
	JSONHistory(const json::Value &data,int maxHist = 100):data(data),maxHist(maxHist) {}


	template<typename Fn>
	void enum_revisions(Fn &&fn) const {
		std::size_t sz = data.size();
		for (std::size_t x = 0; x< sz; x++) {
			fn(x, data[x]);
		}
	}

	json::Value get_rev_by_index(std::size_t idx) const {
		json::Value top = data[0]["c"];
		std::size_t pos = 0;
		while (pos<idx) {
			pos++;
			json::Value diff = data[pos]["c"];
			top = merge_JSON(top, diff);
		}
		return top;
	}

	void add_rev(json::Value new_data, std::time_t time, json::Value user_info = json::Value()) {
		json::Value top = data[0];
		json::Value topc = top["c"];
		json::Value mdata;
		mdata.setItems({
			{"t", time},
			{"u", user_info},
			{"c", new_data},
			{"p", top["h"]}
		});
		std::size_t hash = std::hash<json::Value>()(mdata);
		mdata.setItems({{"h",hash},{"p",json::undefined}});
		if (top.defined()) {
			top.setItems({
				{"c", make_JSON_diff(new_data, topc)},
			});
			data = json::Value(json::array, {
					mdata,top
			}).merge(data.slice(1,maxHist));
		} else {
			data = json::Value(json::array, {mdata});
		}
	}

	static std::time_t get_rev_time(json::Value rev) {return rev["t"].getUIntLong();}
	static std::size_t get_rev_hash(json::Value rev) {return rev["h"].getUInt();}
	static json::Value get_rev_user(json::Value rev) {return rev["u"];}

	json::Value get() const {return data;}

protected:
	json::Value data;
	int maxHist;




};



#endif /* SRC_MAIN_JSONHISTORY_H_ */
