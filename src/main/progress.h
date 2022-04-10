/*
 * progress.h
 *
 *  Created on: 10. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_PROGRESS_H_
#define SRC_MAIN_PROGRESS_H_

#include <string>
#include <optional>

#include <shared/linear_map.h>
#include <shared/shared_object.h>

struct ProgressInfo {
	double percent;
	std::string desc;
};

class ProgressMap {
public:
	using Map = ondra_shared::linear_map<int, ProgressInfo>;

	int alloc() {return id++;}
	void set_percent(int id, double percent);
	void set_desc(int id, const std::string_view &text);
	void free(int id);
	std::optional<ProgressInfo> get(int id) const;

protected:
	int id = 0;
	Map map;
};

using PProgressMap = ondra_shared::SharedObject<ProgressMap>;


class Progress {
public:
	Progress(PProgressMap mp);
	Progress(Progress &&other);
	~Progress();

	double operator=(double percent);
	void operator()(const std::string_view &text);

protected:
	PProgressMap mp;
	int id;

};


#endif /* SRC_MAIN_PROGRESS_H_ */
