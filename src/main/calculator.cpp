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

bool Calculator::addTrade(double eff_price, double b) {

	double c = price2balance(eff_price);
	if (c < b || c > b * 1.1) {
		price = eff_price;
		balance = b;
		return true;
	} else {

		return false;
	}
}

double Calculator::price2balance(double new_price) const {

	return balance*sqrt(price/new_price);

}

double Calculator::balance2price(double new_balance) const {

	return pow2((balance * sqrt(price))/(new_balance));

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
