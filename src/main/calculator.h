/*
 * calculator.h
 *
 *  Created on: 17. 6. 2019
 *      Author: ondra
 */

#ifndef SRC_CALCULATOR_H_
#define SRC_CALCULATOR_H_

namespace json {
	class Value;
}

class Calculator {
public:

	Calculator();
	Calculator(double price, double balance);

	///add trade to calculate internal state
	/**
	 * @param eff_price effective price
	 * @param eff_size effective size
	 * @retval false internal balance did not changed (this is probably ok)
	 * @retval true internal balance changed (probably manual trade)
	 */
	bool addTrade(double eff_price, double abs_balance);

	double balance2price(double balance) const;

	double price2balance(double price) const;

	double calcExtra(double prev_price, double new_price) const;

	json::Value toJSON() const;

	static Calculator fromJSON(json::Value v);




protected:
	double price = 0;
	double balance = 0;

};

#endif /* SRC_CALCULATOR_H_ */
