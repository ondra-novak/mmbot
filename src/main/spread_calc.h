/*
 * spread_calc.h
 *
 *  Created on: 19. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_SPREAD_CALC_H_
#define SRC_MAIN_SPREAD_CALC_H_

#include "../shared/stringview.h"
#include "../shared/worker.h"
#include "istatsvc.h"
#include "istockapi.h"


double glob_calcSpread(ondra_shared::Worker wrk,
		ondra_shared::StringView<IStatSvc::ChartItem> chart,
		const MTrader_Config &config,
		const IStockApi::MarketInfo &minfo,
		double balance,
		double prev_val);


#endif /* SRC_MAIN_SPREAD_CALC_H_ */
