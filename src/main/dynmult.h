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

class DynMultControl {
public:
	struct Config {
		double raise;
		double fall;
		double cap;
		Dynmult_mode mode;
		bool mult;
	};

	DynMultControl(const Config &config):cfg(config),mult_buy(1),mult_sell(1) {}

	void setMult(double buy, double sell);
	double getBuyMult() const;
	double getSellMult() const;

	double raise_fall(double v, bool israise);
	void update(bool buy_trade,bool sell_trade);
	void reset();

protected:

	const Config cfg;
	double mult_buy;
	double mult_sell;

};



#endif /* SRC_MAIN_DYNMULT_H_ */
