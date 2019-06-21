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

bool Calculator::addTrade(double new_price, double abs_balance, bool manual_trade) {


	double eb = price2balance(new_price);
	double bdiff = abs_balance - eb;

	//if balance is less then expected, and not is manual trade, then don't update calculator
	if (bdiff < 0 && !manual_trade) return false;

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

	//formula to map balance to price
	/*
	 * c = b * sqrt(p/n)
	 * c/b = sqrt(p/n)
	 * pow2(c/b) = (p/n)
	 * pow2(c/b)/p = 1/n
	 * p*pow(c/b) = n
	 */
	return price * pow2(new_balance/balance);

}


double Calculator::calcExtra(double last_price, double new_price) const {
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
			json::Value("balance", balance)
	});
}

Calculator::Calculator() {}

Calculator::Calculator(double price, double balance):price(price),balance(balance) {
}

Calculator Calculator::fromJSON(json::Value v) {
	return Calculator(
		v["price"].getNumber(),
		v["balance"].getNumber()
	);
}
