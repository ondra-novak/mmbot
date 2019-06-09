/*
 * ordergen.cpp
 *
 *  Created on: 11. 5. 2019
 *      Author: ondra
 */

#include <cmath>
#include "ordergen.h"

OrderGen::OrderGen(Config config):config(config) {
}

OrderGen::Orders OrderGen::generate(double asset_balance, double buy_price, double middle_price) {


	Orders orders;
	generate_buy(orders, asset_balance, buy_price, middle_price);
	generate_sell(orders, asset_balance, buy_price, middle_price);


	return orders;

}

void OrderGen::generate_buy(Orders& out, double asset_balance, double buy_price, double middle_price) {
	double bp = std::min(buy_price, middle_price);
	double p = bp - config.min_currency;
	double a = adj_amount(amount_from_price(asset_balance, buy_price, p));
	//double step = a;
	double mp = bp /(1 + config.spread);
	p = adj_price(price_from_amount(asset_balance, buy_price, a));
	while (p > mp) {
		if (a > config.min_asset) {
			out.push_back({p, a});
		}
		asset_balance+=a;
		buy_price = p;
		a = 2*a;
		p = adj_price(price_from_amount(asset_balance, buy_price, a));

	}
}

void OrderGen::generate_sell(Orders& out, double asset_balance,
		double buy_price, double middle_price) {

	double bp = std::max(buy_price, middle_price);
	double p = bp + config.min_currency;
	double a = adj_amount(amount_from_price(asset_balance, buy_price, p));
	//double step = a;
	double mp = bp * (1 + config.spread);
	p = adj_price(price_from_amount(asset_balance, buy_price, a));
	while (p < mp && -2*a < asset_balance) {
		if (-a > config.min_asset) {
			out.push_back({p, a});
		}
		asset_balance+=a;
		buy_price = p;
		a = 2*a;
		p = adj_price(price_from_amount(asset_balance, buy_price, a));
	}

}

double OrderGen::amount_from_price(double asset_balance, double buy_price,
	double new_price) {

	//(a*bp - a*np)/(2 * np)
	//a*(bp-np)/(2*np)
	//a*(bp/np-1)/2

	return asset_balance*(buy_price/new_price-1)/2;
}


double OrderGen::price_from_amount(double asset_balance, double buy_price, double add_amount) {

	//na = (a*bp - a*np)/(2 * np)
	//na = (a*bp/2*np) - a/2
	//na*np = (a*bp/2) - a*np/2
	//na*np + a*np/2 = a*bp/2
	//np(na + a/2) = a*bp/2
	//np = a*bp/(2*na + a)

	return asset_balance*buy_price/(2*add_amount + asset_balance);

}

double OrderGen::adj_price(double price) {
	return std::round(price/config.currency_step)*config.currency_step;
}

double OrderGen::adj_amount(double amount) {
	return std::round(amount/config.asset_step)*config.asset_step;
}
