/*
 * strategy3.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_STRATEGY3_H_
#define SRC_MAIN_STRATEGY3_H_
#include <unordered_map>

#include "istrategy3.h"




class Strategy3 {
public:

	Strategy3(PStrategy3 ptr):ptr(ptr) {}
	Strategy3() {}

	void run(AbstractTraderControl &cntr)  {ptr = ptr->run(cntr);}
	void load(const json::Value &state) {ptr = ptr->load(state);}
	json::Value save() const {return ptr->save();}
	ChartPoint get_chart_point(double price) const {return ptr->get_chart_point(price);}
	double calc_initial_position(const MarketState &st) const {return ptr->calc_initial_position(st);}
	std::string_view get_id() const {return ptr->get_id();}
	void reset() {ptr = ptr->reset();}

	static json::NamedEnum<OrderRequestResult> strOrderRequestResult;
protected:
	PStrategy3 ptr;
};



#endif /* SRC_MAIN_STRATEGY3_H_ */
