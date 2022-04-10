/*
 * progress.cpp
 *
 *  Created on: 10. 4. 2022
 *      Author: ondra
 */

#include "progress.h"

void ProgressMap::set_percent(int id, double percent) {
	map[id].percent = percent;
}

void ProgressMap::set_desc(int id, const std::string_view &text) {
	map[id].desc = text;
}


void ProgressMap::free(int id) {
	map.erase(id);
}

Progress::Progress(PProgressMap mp):mp(mp),id(mp.lock()->alloc()) {

}

Progress::Progress(Progress &&other):mp(std::move(mp)),id(std::move(id)) {

}

Progress::~Progress() {
	if (mp != nullptr) mp.lock()->free(id);
}


double Progress::operator =(double percent) {
	mp.lock()->set_percent(id, percent);
	return percent;
}

void Progress::operator ()(const std::string_view &text) {
	mp.lock()->set_desc(id, text);
}

std::optional<ProgressInfo> ProgressMap::get(int id) const {
	auto iter = map.find(id);
	if (iter == map.end()) return {};
	else return iter->second;
}

