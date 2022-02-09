/*
 * aggregate.h
 *
 *  Created on: 4. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_BROKERS_RPTBROKER_AGGREGATE_H_
#define SRC_BROKERS_RPTBROKER_AGGREGATE_H_


#include <optional>
#include <map>

template<typename Key, typename Value, typename KeyCompare>
class AbstractAggregate {
public:

	using Map = std::map<Key, std::optional<Value>, KeyCompare>;

	void invalidate(const Key &k);
	const Value &get(const Key &k);
	const Value &update(typename Map::iterator iter);
	const Value &update(typename Map::const_iterator iter);

	auto begin() const {return map.begin();}
	auto end() const {return map.end();}
	auto lower_bound(const Key &x) const {return map.lower_bound(x);}
	auto upper_bound(const Key &x) const {return map.upper_bound(x);}
	auto find(const Key &x) const {return map.find(x);}

	struct AggRes {
		Value res;
		bool to_cache;
	};

	virtual AggRes reduce(const Key &key) const = 0;

protected:

	Map map;
	std::optional<Value> tmp;


};

template<typename Key, typename Value, typename KeyCompare>
void AbstractAggregate<Key, Value, KeyCompare>::invalidate(const Key &k) {
	map[k] = std::optional<Value>();
}

template<typename Key, typename Value, typename KeyCompare>
const Value &AbstractAggregate<Key, Value, KeyCompare>::get(const Key &k) {
	auto iter = map.find(k);
	if (iter == map.end()) {
		AggRes r = reduce(k);
		if (r.to_cache) {
			iter = map.emplace(k, std::move(r.res)).first;
		} else {
			tmp = std::move(r.res);
			return *tmp;
		}
	}
	return update(iter);
}

template<typename Key, typename Value, typename KeyCompare>
const Value &AbstractAggregate<Key, Value, KeyCompare>::update(typename Map::iterator iter) {
	if (!iter->second.has_value()) {
		iter->second = reduce(iter->first).res;
	}
	return *iter->second;
}

template<typename Key, typename Value, typename KeyCompare>
const Value &AbstractAggregate<Key, Value, KeyCompare>::update(typename Map::const_iterator iter) {
	if (!iter->second.has_value()) {
		return get(iter->first);
	}
	return *iter->second;
}


#endif /* SRC_BROKERS_RPTBROKER_AGGREGATE_H_ */
