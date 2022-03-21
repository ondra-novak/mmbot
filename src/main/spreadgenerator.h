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
	virtual PSpreadGenerator add_point(double price) const = 0;
	virtual PSpreadGenerator report_trade(double price, double size) const = 0;
	virtual PSpreadGenerator reset_dynmult() const = 0;
	virtual double get_order_price(double side, double equilibrium, bool dynmult = true) const = 0;
	virtual double get_base_spread() const = 0;
	virtual json::Value save() const = 0;
	virtual PSpreadGenerator load(json::Value) const = 0;
	virtual std::string_view get_id() const = 0;

};

class ISpreadGeneratorFactory {
public:
	virtual ~ISpreadGeneratorFactory() {}
	virtual PSpreadGenerator create(json::Value cfg) = 0;
	virtual std::string_view get_id() const = 0;
};

class ISpreadGeneratorRegistration {
public:
	virtual ~ISpreadGeneratorRegistration() {}
	virtual void reg(std::unique_ptr<ISpreadGeneratorFactory> &&factory) = 0;
};


class SpreadGenerator {
public:
	SpreadGenerator(PSpreadGenerator ptr):ptr(ptr) {}
	SpreadGenerator();

	void add_point(double price) {ptr = ptr->add_point(price);}
	void report_trade(double price, double size) {ptr=ptr->report_trade(price, size);}
	void reset_dynmult() {ptr=ptr->reset_dynmult();}
	double get_order_price(double side, double equilibrium, bool dynmult = true) const {
		return ptr->get_order_price(side, equilibrium, dynmult);
	}
	double get_base_spread() const {
		return get_base_spread();
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


protected:
	PSpreadGenerator ptr;
};

#endif /* SRC_MAIN_SPREADGENERATOR_H_ */
