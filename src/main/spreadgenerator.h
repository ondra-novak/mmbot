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

class ISpreadGeneratorFactory {
public:
	virtual ~ISpreadGeneratorFactory() {}
	virtual PSpreadGenerator create(json::Value cfg) = 0;
	virtual std::string_view get_id() const = 0;
	virtual json::Value get_form_def() const = 0;
};

class ISpreadGeneratorRegistration {
public:
	virtual ~ISpreadGeneratorRegistration() {}
	virtual void reg(std::unique_ptr<ISpreadGeneratorFactory> &&factory) = 0;
};


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
		return ptr->save();
	}
	void load(json::Value data) {
		ptr = ptr->load(data);
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



protected:
	PSpreadGenerator ptr;
};

class SpreadRegister: public ISpreadGeneratorRegistration {
public:

	SpreadGenerator create(std::string_view id, json::Value config);
	virtual void reg(std::unique_ptr<ISpreadGeneratorFactory> &&factory) override;

	static SpreadRegister &getInstance();

	using SpreadMap = std::unordered_map<std::string_view, std::unique_ptr<ISpreadGeneratorFactory> >;
	auto begin() const {return smap.begin();}
	auto end() const {return smap.end();}
	auto find(const std::string_view &id) const {return smap.find(id);}

protected:

	SpreadMap smap;
};

#endif /* SRC_MAIN_SPREADGENERATOR_H_ */
