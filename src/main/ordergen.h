/*
 * ordergen.h
 *
 *  Created on: 11. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ORDERGEN_H_
#define SRC_MAIN_ORDERGEN_H_
#include <string>
#include <vector>

class OrderGen {
public:

	struct Config {
		double asset_step;
		double currency_step;
		double min_asset;
		double min_currency;
		double spread;
	};




	struct Order {
		double price;
		double size;
		std::string refId;
	};

	using Orders = std::vector<Order>;

	Config config;

	OrderGen(Config config);


	Orders generate(double asset_balance, double buy_price, double middle_price);


protected:
	void generate_buy(Orders &out, double asset_balance, double buy_price, double middle_price);
	void generate_sell(Orders &out, double asset_balance, double buy_price, double middle_price);

	static double amount_from_price(double asset_balance, double buy_price, double new_price);
	static double price_from_amount(double asset_balance, double buy_price, double add_amount);

	double adj_price(double price);
	double adj_amount(double amount);
};

#endif /* SRC_MAIN_ORDERGEN_H_ */
