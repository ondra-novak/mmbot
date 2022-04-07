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

void Utilization::report_reset(const Dur &dur) {
	reset.ellapsed = dur;
	reset.updated = std::chrono::system_clock::now();
}

void Utilization::report_overall(const Dur &dur) {
	overall.ellapsed = dur;
	overall.updated = std::chrono::system_clock::now();
}

void Utilization::report_trader(const std::string_view name, const Dur &dur) {
	auto now = std::chrono::system_clock::now();
	auto iter = map.find(name);
	if (iter == map.end()) {
		map.emplace(std::string(name), UtilInfo{dur, now});
	} else {
		iter->second = UtilInfo {dur, now};
	}
}

json::Value Utilization::getUtilization(std::size_t lastUpdate) const {
	auto lu = std::chrono::system_clock::from_time_t(lastUpdate);
	json::Object res;
	json::Object ids;
	json::Array updated;
	std::chrono::system_clock::time_point lastTime;
	for (const auto &x: map) {
		ids.set(x.first, x.second.ellapsed.count());
		if (x.second.updated > lu) updated.push_back(x.first);
		lastTime = std::max(lastTime, x.second.updated);
	}
	res.set("traders", ids);
	res.set("reset",reset.ellapsed.count());
	res.set("overall",overall.ellapsed.count());
	res.set("updated", updated);
	res.set("last_update", std::chrono::system_clock::to_time_t(lastTime));
	return res;
}




