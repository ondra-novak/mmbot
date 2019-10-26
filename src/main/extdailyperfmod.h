/*
 * extdailyperfmod.h
 *
 *  Created on: 26. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_EXTDAILYPERFMOD_H_
#define SRC_MAIN_EXTDAILYPERFMOD_H_

#include "abstractExtern.h"
#include "idailyperfmod.h"

class ExtDailyPerfMod: public IDailyPerfModule, public AbstractExtern {
public:
	using AbstractExtern::AbstractExtern;

	virtual void sendItem(const PerformanceReport &report) override;
	virtual json::Value getReport() override;

public:
	json::Value reportCache;
	static std::size_t daySeconds;
	std::size_t dayIndex = 0;


};

#endif /* SRC_MAIN_EXTDAILYPERFMOD_H_ */
