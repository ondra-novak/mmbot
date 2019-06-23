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

	///add trade to calculate internal state
	/**
	 * @param new_price new price of the last prder
	 * @param abs_balance absolute balance
	 * @param order_size order size
	 * @retval true calculator adjusted
	 * @retval false calculator not adjusted
	 */
	bool update(double new_price, double abs_balance, bool manual_trade);

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
