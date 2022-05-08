/*
 * jsondiff.cpp
 *
 *  Created on: 8. 5. 2022
 *      Author: ondra
 */


#include <imtjson/object.h>
#include "jsondiff.h"

json::Value merge_JSON(json::Value src, json::Value diff) {
	if (diff.type() == json::object) {
		if (diff.empty()) return json::undefined;
		if (src.type() != json::object) src = json::object;
		auto src_iter = src.begin(), src_end = src.end();
		auto diff_iter = diff.begin(), diff_end = diff.end();
		json::Object out;
		while (src_iter != src_end && diff_iter != diff_end) {
			auto src_v = *src_iter, diff_v = *diff_iter;
			auto src_k = src_v.getKey(), diff_k = diff_v.getKey();
			if (src_k < diff_k) {
				out.set(src_v);
				++src_iter;
			} else if (src_k > diff_k) {
				out.set(diff_k, merge_JSON(json::undefined, diff_v));
				++diff_iter;
			} else {
				out.set(diff_k, merge_JSON(src_v, diff_v));
				++src_iter;
				++diff_iter;
			}
		}
		while (src_iter != src_end) {
			out.set(*src_iter);
			++src_iter;
		}
		while (diff_iter != diff_end) {
			auto diff_v = *diff_iter;
			out.set(diff_v.getKey(), merge_JSON(json::undefined, diff_v));
			++diff_iter;
		}
		return out;
	} else if (diff.type() == json::undefined){
		return src;
	} else {
		return diff;
	}
}

json::Value make_JSON_diff(json::Value src, json::Value trg) {
	if (trg.type() != json::object) {
		if (trg.defined()) {
			if (trg != src)	return trg;
			else return json::undefined;
		}
		else if (src.defined()) return json::object;
		else return json::undefined;
	}
	if (src.type() != json::object) {
		if (trg.type() == json::object && trg.empty()) {
			return json::Value(json::object, {json::Value("",json::object)});
		} else {
			src = json::object;
		}
	}
	auto src_iter = src.begin(), src_end = src.end();
	auto trg_iter = trg.begin(), trg_end = trg.end();
	json::Object out;
	while (src_iter != src_end && trg_iter != trg_end) {
		auto src_v = *src_iter, trg_v = *trg_iter;
		auto src_k = src_v.getKey(), trg_k = trg_v.getKey();
		if (src_k < trg_k) {
			out.set(src_k, make_JSON_diff(src_v, json::undefined));
			++src_iter;
		} else if (src_k > trg_k) {
			out.set(trg_k, make_JSON_diff(json::undefined, trg_v));
			++trg_iter;
		} else {
			out.set(trg_k, make_JSON_diff(src_v, trg_v));
			++src_iter;
			++trg_iter;
		}
	}
	while (src_iter != src_end) {
		auto src_v = *src_iter;
		auto src_k = src_v.getKey();
		out.set(src_k, make_JSON_diff(src_v, json::undefined));
		++src_iter;
	}
	while (trg_iter != trg_end) {
		auto trg_v = *trg_iter;
		auto trg_k = trg_v.getKey();
		out.set(trg_k, make_JSON_diff(json::undefined, trg_v));
		++trg_iter;
	}
	json::Value o = out;
	if (o.empty()) return json::undefined; else return o;

}
