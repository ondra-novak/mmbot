/*
 * ispreadgenerator.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_SPREADGENERATOR_H_
#define SRC_MAIN_SPREADGENERATOR_H_
#include <memory>

#include "shared/refcnt.h"
#include <imtjson/value.h>

#include "tool_register.h"


class ISpreadGenerator;

using PSpreadGenerator = ondra_shared::RefCntPtr<const ISpreadGenerator>;

class ISpreadGenerator: public ondra_shared::RefCntObj {
public:

	virtual ~ISpreadGenerator() {}
	///Add new point - returns new instance
	/**
	 * @param price price of new point
	 * @return new instance
	 */
	virtual PSpreadGenerator add_point(double price) const = 0;
	///Report executed trade
	/**
	 * @param price price of trade
	 * @param size size of trade
	 * @return new instance
	 */
	virtual PSpreadGenerator report_trade(double price, double size) const = 0;
	///Reset dynamic multiplier
	/**
	 * Resets dynamic multiplier (if implemented)
	 * @return new instance
	 */
	virtual PSpreadGenerator reset_dynmult() const = 0;
	///Calculates order price
	/**
	 * @param side side -1 or 1
	 * @param equilibrium price equilibrium
	 * @param dynmult use dynamic multiplicator
	 * @return price
	 */
	virtual double get_order_price(double side, double equilibrium, bool dynmult = true) const = 0;
	///calculate base spread
	virtual double get_base_spread() const = 0;
	///get buy multiplicator
	virtual double get_buy_mult() const = 0;
	///get sell multiplicator
	virtual double get_sell_mult() const = 0;
	///save state
	virtual json::Value save() const = 0;
	///load state
	virtual PSpreadGenerator load(json::Value) const = 0;
	///get id
	virtual std::string_view get_id() const = 0;
	///returns true if state is valid, false if not valid
	virtual bool is_valid() const = 0;

};

using SpreadGenRegister = AbstractToolRegister<PSpreadGenerator>;
using SpreadGenFactory = AbstractToolFactory<PSpreadGenerator>;
using SpreadRegister = ToolRegister<PSpreadGenerator>;

class SpreadGenerator {
public:
	SpreadGenerator(PSpreadGenerator ptr):ptr(ptr) {}
	SpreadGenerator() {}

	void add_point(double price) {ptr = ptr->add_point(price);}
	void report_trade(double price, double size) {ptr=ptr->report_trade(price, size);}
	void reset_dynmult() {ptr=ptr->reset_dynmult();}
	double get_order_price(double side, double equilibrium, bool dynmult = true) const {
		return ptr->get_order_price(side, equilibrium, dynmult);
	}
	double get_base_spread() const {
		return ptr->get_base_spread();
	}
	json::Value save() const {
		return json::Value(json::object,{
				json::Value(ptr->get_id(),ptr->save())
		});

	}
	void load(json::Value state) {
		ptr = ptr->load(state[ptr->get_id()]);
	}
	std::string_view get_id() const {
		return ptr->get_id();
	}
	double get_buy_mult() const {
		return ptr->get_buy_mult();
	}
	double get_sell_mult() const {
		return ptr->get_sell_mult();
	}
	const ISpreadGenerator *operator->() const {
		return ptr;
	}
protected:
	PSpreadGenerator ptr;
};

template<> class ToolName<PSpreadGenerator> {
public:
	static std::string_view get();
};

#endif /* SRC_MAIN_SPREADGENERATOR_H_ */
