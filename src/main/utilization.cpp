/*
 * utilization.cpp
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include "utilization.h"
#include <imtjson/object.h>
#include <imtjson/array.h>

void Utilization::clear() {
	map.clear();
}

void Utilization::report_overall(const Tp &begin, const Tp &end) {
	overall.ellapsed = std::chrono::duration_cast<Dur>(end - begin);
	overall.updated = end;
}

void Utilization::report_trader(const std::string_view name, const Tp &begin, const Tp &end) {
	auto dur = std::chrono::duration_cast<Dur>(end - begin);
	auto iter = map.find(name);
	if (iter == map.end()) {
		map.emplace(std::string(name), UtilInfo{dur, end});
	} else {
		iter->second = UtilInfo {dur, end};
	}
}



json::Value Utilization::UtilInfo::to_json(const Tp &lastCheck) const {
	return  json::Object{
				{"dur",ellapsed.count()},
				{"updated",updated > lastCheck}
			};
}

json::Value Utilization::getUtilization(std::size_t lastUpdate) const {


	auto lu = std::chrono::system_clock::from_time_t(lastUpdate);
	json::Object res;
	json::Object ids;
	std::chrono::system_clock::time_point lastTime = overall.updated;
	for (const auto &x: map) {
		ids.set(x.first, x.second.to_json(lu));
		lastTime = std::max(lastTime, x.second.updated);
	}
	res.set("traders", ids);
	res.set("overall",overall.to_json(lu));
	res.set("tm", std::chrono::system_clock::to_time_t(lastTime)+1);
	return res;
}




