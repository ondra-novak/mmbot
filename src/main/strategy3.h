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
	Strategy3();

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

class StrategyRegister: public AbstractStrategyRegister {
public:

	Strategy3 create(std::string_view id, json::Value config);
	virtual void reg_strategy(std::unique_ptr<AbstractStrategyFactory> &&factory) override;

	static StrategyRegister &getInstance();

	using StrategyMap = std::unordered_map<std::string_view, std::unique_ptr<AbstractStrategyFactory> >;
	auto begin() const {return smap.begin();}
	auto end() const {return smap.end();}
	auto find(const std::string_view &id) const {return smap.find(id);}

protected:

	StrategyMap smap;
};

class UnknownStrategyException: public std::exception {
public:
	UnknownStrategyException(std::string_view id);
	const virtual char* what() const noexcept override;
protected:
	mutable std::string msg;
	std::string id;
};


#endif /* SRC_MAIN_STRATEGY3_H_ */
