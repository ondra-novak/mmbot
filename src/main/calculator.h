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
	Calculator(double price, double balance, bool achieve);


	void update(double new_price, double abs_balance);

	void update_after_trade(double new_price, double new_balance, double old_balance, double acum);

	double balance2price(double balance) const;

	double price2balance(double price) const;

	double calcExtra(double prev_price, double new_price) const;

	json::Value toJSON() const;

	static Calculator fromJSON(json::Value v);

	double getBalance() const {
		return balance;
	}

	double getPrice() const {
		return price;
	}

	bool isValid() const {
		return price > 0 && balance > 0;
	}

	void achieve(double new_price, double new_balance);
	bool isAchieveMode() const {return achieve_mode;}

protected:
	double price = 0;
	double balance = 0;
	bool achieve_mode = false;

};

#endif /* SRC_CALCULATOR_H_ */
