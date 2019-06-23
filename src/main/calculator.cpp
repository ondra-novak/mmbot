/*
 * calculator.cpp
 *
 *  Created on: 17. 6. 2019
 *      Author: ondra
 */

#include <cmath>
#include <imtjson/value.h>

#include "sgn.h"
#include "calculator.h"

bool Calculator::update(double new_price, double abs_balance, bool manual_trade) {


	double eb = price2balance(new_price);
	double bdiff = abs_balance - eb;

	if (achieve_mode) {
		if (std::fabs(bdiff) < balance*0.001) {
			achieve_mode = false;
		} else {
			return false;
		}
	}

	//if balance is less then expected, and not is manual trade, then don't update calculator
	if (bdiff <= 0 && !manual_trade) return false;

	//we will adjust the balance
	price = new_price;
	balance = abs_balance;
	return true;
}

double Calculator::price2balance(double new_price) const {

	//basic formula to map price to balance
	return balance*sqrt(price/new_price);

}

double Calculator::balance2price(double new_balance) const {

	if (new_balance == 0) return 9e99;
	//formula to map balance to price
	/*
	 * c = b * sqrt(p/n)
	 * c/b = sqrt(p/n)
	 * pow2(c/b) = (p/n)
	 * pow2(c/b)/p = 1/n
	 * p*pow(c/b) = n
	 */
	return price * pow2(balance/new_balance);

}


double Calculator::calcExtra(double last_price, double new_price) const {
	if (achieve_mode) return 0;
	//balance after last trade (at last price)
	double b1 = price2balance(last_price);
	//balance at new price
	double b2 = price2balance(new_price);
	//so we must buy (+) or sell (-) that assets
	double sz = b2 - b1;
	//currency need for the trade
	double cur = -sz * new_price;
	//currency change due changed equilibrum
	double cur2 = b2* new_price - b1 * last_price;
	//difference between these currencies (extra profit)
	//divided by new price
	//- extra profit can be used to increase balance
	return (cur - cur2)/new_price;


}

json::Value Calculator::toJSON() const {
	return json::Value(json::object,{
			json::Value("price", price),
			json::Value("balance", balance),
			json::Value("achieve", achieve_mode)
	});
}


void Calculator::achieve(double new_price, double new_balance) {
	achieve_mode = true;
	price = new_price;
	balance = new_balance;
}

Calculator::Calculator() {}

Calculator::Calculator(double price, double balance, bool achieve):price(price),balance(balance),achieve_mode(achieve) {
}

Calculator Calculator::fromJSON(json::Value v) {
	return Calculator(
		v["price"].getNumber(),
		v["balance"].getNumber(),
		v["achieve"].getBool()
	);
}
