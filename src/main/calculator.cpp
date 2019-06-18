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

bool Calculator::addTrade(double new_price, double abs_balance, double order_size) {


	double eb = price2balance(new_price);
	double bdiff = abs_balance - eb;
	//if difference is very small
	if (fabs(bdiff) < fabs(abs_balance+eb)*1e-10) return false;
	//if the buy order was too small on given price - we can compensate
	if (bdiff < 0 && order_size > 0) return false;
	//so if the balance was manually lowered, because order size was negative
	//or if the balance was manually raised
	//we will adjust the balance
	price = new_price;
	balance = abs_balance;
	return true;
}

double Calculator::price2balance(double new_price) const {

	return balance*sqrt(price/new_price);

}

double Calculator::balance2price(double new_balance) const {

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
	double b1 = price2balance(last_price);
	double b2 = price2balance(new_price);
	double sz = b2 - b1;
	double vol = -sz * new_price;
	double vol2 = b2* new_price - b1 * last_price;
	return (vol - vol2)/new_price;


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
