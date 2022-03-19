/*
 * ispreadgenerator.h
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISPREADGENERATOR_H_
#define SRC_MAIN_ISPREADGENERATOR_H_
#include "shared/refcnt.h"


class ISpreadGenerator;

using PSpreadGenerator = ondra_shared::RefCntPtr<ISpreadGenerator>;

class ISpreadGenerator: public ondra_shared::RefCntObj {
public:

	virtual PSpreadGenerator add_point(double price) const = 0;
	virtual PSpreadGenerator report_trade(double price, double size) const = 0;
	virtual PSpreadGenerator reset_dynmult() const = 0;
	virtual double get_order_price(double side, double equilibrium, bool dynmult = true) const = 0;
	virtual double get_base_spread() const;
	virtual ~ISpreadGenerator() {}
};



#endif /* SRC_MAIN_ISPREADGENERATOR_H_ */
