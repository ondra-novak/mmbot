#pragma once

#ifndef MMBOT_SRC_LONGRUN_H
#define MMBOT_SRC_LONGRUN_H
#include <deque>
#include <userver/callback.h>
#include <shared/linear_map.h>
#include "ssestream.h"

struct RegOpMap {
		int next_id = 0;
		using CB = userver::Callback<void(PSSEStream, int id)>;
		std::deque<CB> reg_list;
		int push(CB &&x) {
			reg_list.push_back(std::move(x));
			auto out = next_id++;
			if (reg_list.size()>64) reg_list.pop_back();
			return out;
		}
		bool check(int id) const {
			auto pos = id - next_id + reg_list.size();
			if (pos >= 0 && id < next_id) return reg_list[pos] != nullptr;
			else return false;
		}
		CB pop(int id)  {
			auto pos = id - next_id + reg_list.size();
			if (pos >= 0 && id < next_id) {
				CB out;
				std::swap(out, reg_list[pos]);
				return out;
			}
			else return CB();
		}
	};

	struct CancelMap {
		ondra_shared::linear_map<int, bool> map;
		bool is_canceled(int id) const {
			auto iter = map.find(id);
			return iter != map.end() && iter->second;
		}
		bool lock(int id) {
			return map.emplace(id, false).second;
		}
		void unlock(int id) {
			map.erase(id);
		}
		bool set_canceled(int id) {
			auto iter = map.find(id);
			if (iter != map.end()) {iter->second = true;return true;}
			else return false;
		}
	};


	using PCancelMap = ondra_shared::SharedObject<CancelMap>;

	using PRegOpMap = ondra_shared::SharedObject<RegOpMap>;

#endif
