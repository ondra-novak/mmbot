/*
 * dynmult.h
 *
 *  Created on: Aug 19, 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_DYNMULT_H_
#define SRC_MAIN_DYNMULT_H_
#include <imtjson/namedEnum.h>

enum class Dynmult_mode {
	disabled,
	independent,
	together,
	alternate,
	half_alternate,
};


extern json::NamedEnum<Dynmult_mode> strDynmult_mode;

class DynMult {
public:

	struct Config {
		double raise;
		double fall;
		double cap;
		Dynmult_mode mode;
		bool mult;
	};

	DynMult():mult_buy(1.0),mult_sell(1.0) {}
	DynMult(double mult_buy,double mult_sell):mult_buy(mult_buy),mult_sell(mult_sell) {}

	double getBuyMult() const {return mult_buy;}
	double getSellMult() const {return mult_sell;}
	static double raise_fall(const Config &cfg, double v, bool israise);
	DynMult update(const Config &cfg, bool buy_trade,bool sell_trade) const;



protected:
	double mult_buy;
	double mult_sell;

};


class DynMultControl {
public:

	using Config = DynMult::Config;

	DynMultControl(const Config &config):cfg(config) {}

	void setMult(double buy, double sell);
	double getBuyMult() const;
	double getSellMult() const;

	double raise_fall(double v, bool israise) const;
	void update(bool buy_trade,bool sell_trade);
	void reset();

protected:

	Config cfg;
	DynMult state;

};



#endif /* SRC_MAIN_DYNMULT_H_ */
