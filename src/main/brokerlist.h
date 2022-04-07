/*
 * ibrokerlist.h
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_BROKERLIST_H_
#define SRC_MAIN_BROKERLIST_H_

#include <userver/callback.h>
#include "istockapi.h"
#include "apikeys.h"
#include "ibrokercontrol.h"




class AbstractBrokerList {
public:
	virtual ~AbstractBrokerList() {}

	using EnumFn = userver::Callback<void(std::string_view, PStockApi)>;

	virtual PStockApi get_broker(const std::string_view &name) = 0;
	virtual void enum_brokers(EnumFn &&fn) const = 0;
};


using PBrokerList = std::shared_ptr<AbstractBrokerList>;

#endif /* SRC_MAIN_BROKERLIST_H_ */
