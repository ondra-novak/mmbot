/*
 * alert.h
 *
 *  Created on: 12. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ALERT_H_
#define SRC_MAIN_ALERT_H_


enum class AlertReason {
	///Unknown reason
	unknown = 0,
	///Order size would be below minimal allowed size
	below_minsize,
	///Strategy state is out of sync (intentionally or not)
	strategy_outofsync,
	///Max leverage reached
	max_leverage,
	///Max cost reached
	max_cost,
	///Position limit reached
	position_limit,
	///Trade out of budget (must be enabled)
	out_of_budget,
	///Accept loss in effect
	accept_loss,
	///Enforced by strategy
	strategy_enforced,
	///Enforced by strategy
	no_funds,
	///Alert by reset
	initial_reset
};

struct AlertInfo {
	double price;
	AlertReason reason;
};


#endif /* SRC_MAIN_ALERT_H_ */
