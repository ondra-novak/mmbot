/*
 * market_info.cpp
 *
 *  Created on: 28. 4. 2022
 *      Author: ondra
 */

#include <cmath>
#include <imtjson/object.h>
#include "sgn.h"
#include "market_info.h"

json::NamedEnum<FeeScheme> strFeeScheme ({
	{FeeScheme::currency, "currency"},
	{FeeScheme::assets, "assets"},
	{FeeScheme::income, "income"},
	{FeeScheme::outcome, "outcome"}
});

json::NamedEnum<MarketType> strMarketType ({
	{MarketType::normal, "norm"},
	{MarketType::inverted, "inv"}
});
/*
static double nearZero(double v) {
	return sgn(v) * floor(std::abs(v));
}
*/

static double farZero(double v) {
	return sgn(v) * ceil(std::abs(v));
}


double MarketInfo::priceAddFees(double price, double side) const {
	switch (type) {
	case MarketType::normal:price = price*(1 - side*fees);break;
	case MarketType::inverted: price = price/(1-side*fees);break;
	default: break;
	}

	auto roundFn = [&](double x) {
		return side<0?floor(x):side>0?ceil(x):0;
	};

	price = adjValue(price, currency_step, roundFn);
	return price;
}

double MarketInfo::priceRemoveFees(double price, double side) const{
	switch (type) {
	case MarketType::normal:return price/(1- side*fees);
	case MarketType::inverted: return price*(1- side*fees);
	default: return price;
	}

}

void MarketInfo::addFees(double &assets, double &price) const {
	//always shift price
    price = priceAddFees(price,sgn(assets));
	switch (feeScheme) {
	case FeeScheme::currency:
				   break;
	case FeeScheme::assets:
					assets = assets*(1+fees);
					break;
	case FeeScheme::income:
					if (assets>0 ) {
						assets = assets*(1+fees);
					}
					break;
	case FeeScheme::outcome:
					if (assets<0 ) {
						assets = assets*(1+fees);
					}
					break;
	}

	assets = adjValue(assets, asset_step, farZero);
}

void MarketInfo::removeFees(double &assets, double &price) const {
	price = priceRemoveFees(price, sgn(assets));
	switch (feeScheme) {
	case FeeScheme::currency:
				   break;
	case FeeScheme::assets:
					assets = assets/(1+fees);
					break;
	case FeeScheme::income:
					if (assets>0 ) assets = assets/(1+fees);
					break;
	case FeeScheme::outcome:
					if (assets<0 ) assets = assets/(1+fees);
					break;
	}
}

json::Value MarketInfo::toJSON() const {
return json::Object({
	{"asset_step",asset_step},
	{"currency_step", currency_step},
	{"asset_symbol",asset_symbol},
	{"currency_symbol", currency_symbol},
	{"min_size", min_size},
	{"min_volume", min_volume},
	{"fees", fees},
	{"feeScheme",strFeeScheme[feeScheme]},
	{"leverage", leverage},
	{"invert_price", invert_price},
	{"quoted_symbol", quoted_symbol},
	{"simulator", simulator},
	{"private_chart", private_chart},
	{"type", strMarketType[type]},
	{"wallet_id", wallet_id}});
}
MarketInfo MarketInfo::fromJSON(const json::Value &v) {
	MarketInfo res;
	res.asset_step = v["asset_step"].getNumber();
	res.currency_step = v["currency_step"].getNumber();
	res.asset_symbol = v["asset_symbol"].getString();
	res.currency_symbol = v["currency_symbol"].getString();
	res.min_size = v["min_size"].getNumber();
	res.min_volume= v["min_volume"].getNumber();
	res.fees = v["fees"].getNumber();
	res.feeScheme = strFeeScheme[v["feeScheme"].getString()];
	res.leverage= v["leverage"].getNumber();
	res.invert_price= v["invert_price"].getBool();
	res.simulator= v["simulator"].getBool();
	res.quoted_symbol= v["quoted_symbol"].getString();
	res.private_chart = v["private_chart"].getBool();
	res.wallet_id = v["wallet_id"].getString();
	res.type = strMarketType[v["type"].getString()];
	return res;
}

std::int64_t MarketInfo::priceToTick(double price) const {
	return static_cast<std::int64_t>(std::round(price/currency_step));
}
double MarketInfo::tickToPrice(std::int64_t tick) const {
	return tick*currency_step;
}

double MarketInfo::calcEquity(double assets, double balance, double price) const {
	if (leverage) return balance;
	else switch (type) {
		default:
		case MarketType::normal: return balance + price *assets;
		case MarketType::inverted: return balance - assets / price;
	}
}

double MarketInfo::calcEquity(double assets, double balance, double price0, double price1) const {
	if (leverage) {
		switch (type) {
			default:
			case MarketType::normal: return balance + assets * (price1 - price0);
			case MarketType::inverted: return balance + assets * (1.0/price0 - 1.0/price1);
		}
	}
	else {
		switch (type) {
			default:
			case MarketType::normal: return balance + price1 *assets;
			case MarketType::inverted: return balance - assets / price1;
		}
	}
}

double MarketInfo::calcProfit(double position, double price_enter, double price_exit) const {
	switch (type) {
		default:
		case MarketType::normal: return position * (price_exit - price_enter);
		case MarketType::inverted: return position * (1.0/price_enter - 1.0/price_exit);
	}
}

double MarketInfo::getMinSize(double price) const {
	return std::max<double>({asset_step, min_size, calcPosFromValue(min_volume, price)});
}

double MarketInfo::calcPosValue(double position, double price) const {
	switch (type) {
	default:
	case MarketType::normal: return std::abs(position * price);
	case MarketType::inverted: return std::abs(position / price);
	}
}

double MarketInfo::calcCurrencyChange(double size, double price, bool no_leverage) const {
	if (leverage && !no_leverage) return 0;
	switch (type) {
	default:
	case MarketType::normal: return -size * price;
	case MarketType::inverted: return size/ price;
	}
}

double MarketInfo::sizeFromCurrencyChange(double change, double price) const {
	switch (type) {
	default:
	case MarketType::normal: return -change/price;
	case MarketType::inverted: return  change * price;
	}

}

double MarketInfo::calcPosFromValue(double value, double price) const {
	switch (type) {
	default:
	case MarketType::normal: return value / price;
	case MarketType::inverted: return value * price;
	}
}

double calc_profit(MarketType tp, double pos, double enter, double exit) {
	switch (tp) {
	case MarketType::inverted: return pos*(1.0/enter - 1.0/exit);
		default:
		case MarketType::normal: return pos * (exit - enter);
	}
}

double calc_spot_equity(MarketType tp, double balance, double pos, double price) {
	switch (tp) {
	    case MarketType::inverted: return balance - pos / price;
		default:
		case MarketType::normal: return + pos * price;
	}
}


ACB MarketInfo::initACB (double open_price, double position, double realized_pnl) const {
	return ACB(type == MarketType::inverted, open_price, position, realized_pnl);
}
ACB MarketInfo::initACB () const {
	return ACB(type == MarketType::inverted, 0.0, 0.0, 0.0);
}
