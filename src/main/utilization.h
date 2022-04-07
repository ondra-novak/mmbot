/*
 * utilization.h
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_UTILIZATION_H_
#define SRC_MAIN_UTILIZATION_H_

#include <chrono>
#include <string_view>
#include <shared/linear_map.h>
#include <imtjson/value.h>
#include <shared/shared_object.h>

class Utilization {
public:

	using Dur = std::chrono::milliseconds;
	using Tp = std::chrono::system_clock::time_point;
	struct UtilInfo {
		Dur ellapsed;
		Tp updated;
	};
	using Map = ondra_shared::linear_map<std::string, UtilInfo , std::less<> >;

	void clear();
	void report_reset(const Dur &dur);
	void report_overall(const Dur &dur);
	void report_trader(const std::string_view name, const Dur &dur);
	json::Value getUtilization(std::size_t lastUpdate) const;


protected:

	Map map;
	UtilInfo reset;
	UtilInfo overall;
};


using PUtilization = ondra_shared::SharedObject<Utilization>;


#endif /* SRC_MAIN_UTILIZATION_H_ */
