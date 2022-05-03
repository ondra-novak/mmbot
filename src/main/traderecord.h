/*
 * structs.h
 *
 *  Created on: 2. 5. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_TRADERECORD_H_
#define SRC_MAIN_TRADERECORD_H_
#include <cmath>

#include "istockapi.h"


	struct TradeRecord {

		///Trade identifier - It used to specify last seen trade while syncing
		json::Value id;
		///Time of creation (in milliseconds)
		std::uint64_t time;
		///Amount of assets has been traded
		/** The value is NEGATIVE for sell trade or POSITIVE for buy trade */
	    double size;
	    ///price for one item
	    double price;
	    ///effective size after applying fees
	    double eff_size;
	    ///effective price after applying fees
	    double eff_price;
	    ///norm profit
		double norm_profit;
		///norm accum
		double norm_accum;
		///neutral price
		double neutral_price;
		bool manual_trade = false;
		char alertSide;
		char alertReason;

		TradeRecord(const IStockApi::Trade &t, double norm_profit, double norm_accum, double neutral_price, bool manual = false, char as = 0, char ar = 0)
			:id(t.id)
			,time(t.time)
			,size(t.size)
			,price(t.price)
			,eff_size(t.eff_size)
			,eff_price(t.eff_price)
			,norm_profit(norm_profit),norm_accum(norm_accum),neutral_price(neutral_price),manual_trade(manual),alertSide(as),alertReason(ar) {}


		static double fix_number(double x) {
			if (!std::isfinite(x)) x = 0;
			return x;
		}

	    static TradeRecord fromJSON(json::Value v) {
	    	return TradeRecord (IStockApi::Trade{
	    		v["id"].stripKey(),
	    		v["time"].getUIntLong(),
	    		v["size"].getNumber(),
	    		v["price"].getNumber(),
	    		v["eff_size"].getNumber(),
	    		v["eff_price"].getNumber(),
				nullptr
	    	},
				fix_number(v["np"].getNumber()),
				fix_number(v["ap"].getNumber()),
				fix_number(v["p0"].getNumber()),
				v["man"].getBool(),
				static_cast<char>(v["as"].getInt()),
				static_cast<char>(v["ar"].getInt())
	    	);
	    }
	    json::Value toJSON() const {
	    	json::Value ret(json::object);
	    	ret.setItems({
	    			{"size", size},
	    			{"time",time},
	    			{"price",price},
	    			{"eff_price",eff_price},
	    			{"eff_size",eff_size},
	    			{"id",id},
					{"np",norm_profit},
					{"ap",norm_accum},
					{"p0",neutral_price},
	    			{"man",manual_trade?json::Value(true):json::Value()},
					{"as", alertSide == 0?json::Value():json::Value(alertSide)},
					{"ar", alertReason == 0?json::Value():json::Value(alertReason)}
	    	});
	    	return ret;
	    }


	};


#endif /* SRC_MAIN_TRADERECORD_H_ */
