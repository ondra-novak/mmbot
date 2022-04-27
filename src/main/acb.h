/*
 * acb.h
 *
 *  Created on: 11. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ACB_H_
#define SRC_MAIN_ACB_H_

///Calculates UPnL and RPnL based on trades
/**
 * Object is immutable. Every trade generates new instance
 */
class ACB {
public:



	///Initialize object (deprecated)
	/**
	 * @param init_price initial price where init_pos has been opened. If the init_pos is zero,
	 *  this value is ignored
	 * @param init_pos initial position
	 * @param init_rpnl initializes RPnL
	 */
	ACB(double init_price, double init_pos, double init_rpnl = 0)
		:inverted(false)
		,suma(init_pos && init_price?init_price * init_pos:0)
		,pos(init_pos)
		,rpnl(init_rpnl)
	{}

	///Initialize object (new - also calculates profit for inverted markets)
	/**
	 * @param inverted true - inverted market, false - normal market
	 * @param init_price initial price where init_pos has been opened. If the init_pos is zero,
	 *  this value is ignored
	 * @param init_pos initial position
	 * @param init_rpnl initializes RPnL
	 */
	ACB(bool inverted, double init_price, double init_pos, double init_rpnl = 0)
		:inverted(inverted)
		,suma(init_pos && init_price?(inverted?-init_pos/init_price:init_price * init_pos):0)
		,pos((inverted?-1.0:1.0)+init_pos)
		,rpnl(init_rpnl)
	{}

	///Record trade at price and size. The result is returned as new state
	/**
	 * @param price price of the trade
	 * @param size size of the trade
	 * @return new state
	 *
	 * Fee must be substracted. Effective price and size must be passed
	 */
	ACB operator()(double price, double size) const {
		if (inverted) {
			price = 1.0/price;
			size = -size;
		}
		ACB res(inverted);
		if (pos * size >= 0) {
			res.suma = suma + size * price;;
			res.pos = pos + size;
			res.rpnl = rpnl;
		} else if (pos * (pos + size) < 0) {
			double avg = suma/pos;
			res.rpnl = rpnl + (price - avg) * pos;
			size += pos;
			res.pos = 0;
			res.suma = 0;
			return res(price,size);
		} else {
			if (std::abs(pos + size) < (std::abs(pos)+std::abs(size))*1e-10) {
				size = -pos; //solve rounding errors
			}
			double avg = suma/pos;
			res.rpnl = rpnl - (price - avg) * size;
			res.pos = pos + size;
			res.suma = avg * res.pos;
		}
		return res;
	}

	ACB resetRPnL() const {
		ACB res(inverted);
		res.suma = suma;
		res.pos = pos;
		res.rpnl = 0;
		return res;
	}

	///Retrieve open price
	double getOpen() const {
		if (inverted) return pos/suma;else return suma/pos;}
	double getPos() const {return inverted?-pos:pos;}
	///Get current realized PnL
	double getRPnL() const {return rpnl;}
	///Get current unrealized PnL
	double getUPnL(double price) const {
		if (inverted) price = 1.0/price;
		return price*pos - suma;
	}
	///Get current equity
	double getEquity(double price) const {return getRPnL()+getUPnL(price);}
	///Determine whether it calculates inverted market;
	bool isInverted() const {return inverted;}

protected:
	ACB(bool inverted):inverted(inverted) {}

	bool inverted;
	double suma;
	double pos;
	double rpnl;

};



#endif /* SRC_MAIN_ACB_H_ */
