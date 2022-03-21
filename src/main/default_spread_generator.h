/*
 * default_spread_generator.h
 *
 *  Created on: 18. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_DEFAULT_SPREAD_GENERATOR_H_
#define SRC_MAIN_DEFAULT_SPREAD_GENERATOR_H_
#include "shared/refcnt.h"
#include "dynmult.h"
#include "spreadgenerator.h"

class AdaptiveSpreadGenerator: public ISpreadGenerator {
public:


	struct Config: public DynMult::Config {
		double sma_interval;
		double stdev_interval;
		double mult;
		bool sliding;
		bool freeze;
	};


	AdaptiveSpreadGenerator(const Config &cfg);
	virtual double get_order_price(double side, double equilibrium, bool dynmult) const override;
	virtual PSpreadGenerator add_point(double price) const override;
	virtual PSpreadGenerator reset_dynmult() const override;
	virtual double get_base_spread() const override;
	virtual PSpreadGenerator report_trade(double price, double size) const override;
	virtual json::Value save() const override;
	virtual PSpreadGenerator load(json::Value) const override;
	virtual std::string_view get_id() const override {return id;}

	static std::string_view id;

	static void reg(ISpreadGeneratorRegistration &reg);


protected:

	class RefCntCfg: public ondra_shared::RefCntObj, public Config {
	public:
		RefCntCfg(const Config &cfg):Config(cfg) {}
	};
	using PConfig = ondra_shared::RefCntPtr<RefCntCfg>;

	struct State {
		bool valid;
		DynMult dynState;
		double ema;
		double ema2;
		double trade_base;
		double freeze;
	};

	PConfig cfg;
	State state;

	AdaptiveSpreadGenerator(const PConfig &cfg, State &&state);
};


class FixedSpreadGenerator: public ISpreadGenerator {
public:

	struct Config: public DynMult::Config {
		double spread_pct;
	};

	FixedSpreadGenerator(const Config &cfg);
	virtual double get_order_price(double side, double equilibrium, bool dynmult) const override;
	virtual PSpreadGenerator add_point(double price) const override;
	virtual PSpreadGenerator reset_dynmult() const override;
	virtual PSpreadGenerator report_trade(double price, double size) const override;
	virtual double get_base_spread() const override;
	virtual json::Value save() const override;
	virtual PSpreadGenerator load(json::Value) const override;
	virtual std::string_view get_id() const override {return id;}

	static void reg(ISpreadGeneratorRegistration &reg);

	static std::string_view id;
protected:

	class RefCntCfg: public ondra_shared::RefCntObj, public Config {
	public:
		RefCntCfg(const Config &cfg):Config(cfg) {}
	};
	using PConfig = ondra_shared::RefCntPtr<RefCntCfg>;


	struct State {
		DynMult dynState;
	};

	PConfig cfg;
	State state;


	FixedSpreadGenerator(const PConfig &cfg, State &&state);

};



#endif /* SRC_MAIN_DEFAULT_SPREAD_GENERATOR_H_ */
